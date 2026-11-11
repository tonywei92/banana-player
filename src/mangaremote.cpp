// The servers' catalogue model, pure (see the header): opds.h reads the bodies, json.h only writes, and everything
// here turns what was read into the source-script shapes or into the next request. Nothing fetches, nothing waits.
#include "mangaremote.h"
#include <cctype>
#include <cstddef>
#include "manga_names.h"
#include "opds.h"
#include "secret.h"

namespace bp
{
	namespace
	{
		// RFC 3986: the unreserved set goes through untouched and everything else becomes %XX. Search text and ids
		// are server data like any other, so no character below the byte level gets trusted into a URL — an id with
		// a "?" in it must not be able to open a page that isn't that id.
		std::string pct(const std::string& s)
		{
			static const char* const hex = "0123456789ABCDEF";
			std::string o;
			o.reserve(s.size());
			for (unsigned char c : s)
			{
				if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') o += (char)c;
				else
				{
					o += '%';
					o += hex[c >> 4];
					o += hex[c & 15];
				}
			}
			return o;
		}

		void setAccept(RemoteRequest& r, const char* accept)
		{
			for (std::pair<std::string, std::string>& h : r.headers)
				if (h.first == "Accept")
				{
					h.second = accept;
					return;
				}
			r.headers.emplace_back("Accept", accept);
		}

		// opds.cpp keeps its scanner helpers to itself, the same way comicinfo's helpers stay there, so the replace
		// code lives here again rather than editing opds for a shared header
		void subAll(std::string& s, const std::string& from, const std::string& to)
		{
			if (from.empty()) return;
			size_t p = 0;
			while ((p = s.find(from, p)) != std::string::npos)
			{
				s.replace(p, from.size(), to);
				p += to.size();
			}
		}

		// an OpenSearch description is not an Atom feed, so parseOpds won't read it; it is a handful of attributes,
		// and the one we want is escaped markup: a parameter separator is written &amp; there and "&" here
		std::string decodeEntities(const std::string& s)
		{
			std::string o;
			for (size_t i = 0; i < s.size();)
			{
				if (s[i] == '&')
				{
					size_t semi = s.find(';', i + 1);
					if (semi != std::string::npos && semi - i <= 10)
					{
						std::string name = s.substr(i + 1, semi - i - 1);
						if (name == "amp") { o += '&'; i = semi + 1; continue; }
						if (name == "lt") { o += '<'; i = semi + 1; continue; }
						if (name == "gt") { o += '>'; i = semi + 1; continue; }
						if (name == "quot") { o += '"'; i = semi + 1; continue; }
						if (name == "apos") { o += '\''; i = semi + 1; continue; }
					}
				}
				o += s[i++];
			}
			return o;
		}

		// the entry's best cover, absolute: the full image where the server offers one, its thumbnail otherwise
		std::string opdsCover(const std::string& feedUrl, const OpdsEntry& e)
		{
			std::string c = e.cover();
			if (c.empty()) c = e.thumbnail();
			return c.empty() ? std::string() : opdsResolve(feedUrl, c);
		}

		// the page count a stream link carries (thr:count, pse:count — the parser already folded them onto the
		// link); matched by the href, since that is the very template this returns a count for
		int pseCount(const OpdsEntry& e, const std::string& pse)
		{
			for (const OpdsLink& l : e.links)
				if (l.href == pse && l.count >= 0) return l.count;
			return -1;
		}

		// the description's query template: the first one carrying {searchTerms}, in either brace spelling
		// (opdsPseUrl met the same doubling). Descriptions list a self url and the like before it, so scanning
		// for the placeholder, not for the first template, is the difference between searching and reloading
		// the description forever.
		std::string searchTemplate(const std::string& descXml, const std::string& descUrl)
		{
			size_t p = 0;
			while ((p = descXml.find("template=", p)) != std::string::npos)
			{
				p += 9;
				size_t open = descXml.find_first_of("\"'", p);
				if (open == std::string::npos) break;
				for (size_t i = p; i < open; i++)
					if (descXml[i] != ' ' && descXml[i] != '\t' && descXml[i] != '\r' && descXml[i] != '\n')
					{
						open = std::string::npos;	// the name ends here was only text
						break;
					}
				if (open == std::string::npos) continue;
				size_t close = descXml.find(descXml[open], open + 1);
				if (close == std::string::npos) break;
				std::string t = decodeEntities(descXml.substr(open + 1, close - open - 1));
				p = close + 1;
				if (t.find("{searchTerms}") != std::string::npos || t.find("%7BsearchTerms%7D") != std::string::npos
				    || t.find("%7bsearchTerms%7d") != std::string::npos)
					return opdsResolve(descUrl, t);
			}
			return "";
		}

		// Komga's own ordering value where it gave one; the name's number for extras and for servers that sort
		// nothing ("Chapter 12.5", and the way the card's own chapters are numbered)
		double komgaNumber(const KomgaItem& it)
		{
			if (it.numberSort >= 0) return it.numberSort;
			return chapterNumber(it.name);
		}
	}

	std::string remoteWithKey(const MangaServer& s, const std::string& password, const std::string& url)
	{
		// Kavita's stored url names its api key only by the placeholder (see mangaservers.h): the key itself goes in
		// here, at request time, and nowhere else
		if (s.kind != "kavita" || password.empty()) return url;
		std::string out = url;
		subAll(out, "{key}", password);
		return out;
	}

	std::string remoteWithoutKey(const MangaServer& s, const std::string& password, const std::string& text)
	{
		// Kavita answers with its own address in every href, key and all: the key becomes {key} before anything
		// reads the body, so what is kept (ids, hrefs, page urls) never holds the secret
		if (s.kind != "kavita" || password.empty()) return text;
		std::string out = text;
		subAll(out, password, "{key}");
		return out;
	}

	RemoteRequest remoteAuth(const MangaServer& s, const std::string& password, const std::string& url)
	{
		RemoteRequest r;
		r.url = remoteWithKey(s, password, url);
		// Kavita's api key is in the request url (see above), so sending it again as a header would only put a secret
		// on the wire twice; the other kinds use the user, when set
		if (s.kind != "kavita" && !s.user.empty())
			r.headers.emplace_back("Authorization", "Basic " + base64Encode(s.user + ":" + password));
		r.headers.emplace_back("Accept", s.kind == "komga" ? "application/json" : "application/atom+xml");
		return r;
	}

	RemoteRequest remoteKomgaLibraries(const MangaServer& s, const std::string& password)
	{
		return remoteAuth(s, password, komgaUrl(s.url, "/api/v1/libraries"));
	}

	RemoteRequest remoteKomgaSeries(const MangaServer& s, const std::string& password, const std::string& libraryId, int page)
	{
		// 40 to a page: a cover grid fills the panel in a handful of fetches, and one page is one answer to digest
		return remoteAuth(s, password, komgaUrl(s.url, "/api/v1/series?library_id=" + pct(libraryId) + "&page="
		                                          + std::to_string(page) + "&size=40&sort=metadata.titleSort"));
	}

	RemoteRequest remoteKomgaSearch(const MangaServer& s, const std::string& password, const std::string& query, int page)
	{
		// no title sort here: Komga ranks matches, and re-sorting alphabetically would bury the best one
		return remoteAuth(s, password, komgaUrl(s.url, "/api/v1/series?search=" + pct(query) + "&page="
		                                          + std::to_string(page) + "&size=40"));
	}

	RemoteRequest remoteKomgaBooks(const MangaServer& s, const std::string& password, const std::string& seriesId, int page)
	{
		// 100 to a page: the series page wants the whole chapter list in as few fetches as it can get
		return remoteAuth(s, password, komgaUrl(s.url, "/api/v1/series/" + pct(seriesId) + "/books?page="
		                                          + std::to_string(page) + "&size=100&sort=metadata.numberSort"));
	}

	RemoteRequest remoteKomgaPage(const MangaServer& s, const std::string& password, const std::string& bookId, int page)
	{
		if (page < 1) return RemoteRequest();	// Komga numbers its pages from 1; asking below that is a bug, not a request
		RemoteRequest r = remoteAuth(s, password,
		                             komgaUrl(s.url, "/api/v1/books/" + pct(bookId) + "/pages/" + std::to_string(page)));
		setAccept(r, "image/*");	// a page is a picture whatever the rest of the api answers with
		return r;
	}

	RemoteRequest remoteKomgaSeriesCover(const MangaServer& s, const std::string& password, const std::string& seriesId)
	{
		RemoteRequest r = remoteAuth(s, password, komgaUrl(s.url, "/api/v1/series/" + pct(seriesId) + "/thumbnail"));
		setAccept(r, "image/*");
		return r;
	}

	RemoteRequest remoteKomgaBookCover(const MangaServer& s, const std::string& password, const std::string& bookId)
	{
		RemoteRequest r = remoteAuth(s, password, komgaUrl(s.url, "/api/v1/books/" + pct(bookId) + "/thumbnail"));
		setAccept(r, "image/*");
		return r;
	}

	bool remoteKomgaLibraryList(const std::string& json, RemoteList& out)
	{
		// the libraries endpoint answers with a bare array, unlike every other REST list with its {"content":…}
		// page: wrapped in the same shape, one reader takes both
		std::string body = json;
		size_t a = body.find_first_not_of(" \t\r\n");
		if (a != std::string::npos && body[a] == '[') body = "{\"content\":" + body + ",\"totalPages\":1}";
		std::vector<KomgaItem> items;
		int totalPages = 0;
		if (!parseKomgaPage(body, items, totalPages)) return false;
		out = RemoteList();
		for (const KomgaItem& it : items)
		{
			if (it.id.empty()) continue;	// a library without a key can't be asked about again
			RemoteItem ri;
			ri.key = it.id;
			ri.title = it.name;
			ri.folder = true;
			out.items.push_back(std::move(ri));
		}
		return true;
	}

	bool remoteKomgaSeriesList(const std::string& baseUrl, const std::string& json, int page, RemoteList& out)
	{
		std::vector<KomgaItem> items;
		int totalPages = 0;
		if (!parseKomgaPage(json, items, totalPages)) return false;
		out = RemoteList();
		for (const KomgaItem& it : items)
		{
			if (it.id.empty()) continue;
			RemoteItem ri;
			ri.key = it.id;
			ri.title = it.name;
			// the cover is a url, not a fetch: a grid that never rests asks for it like any other cover, with
			// remoteAuth's headers
			ri.cover = komgaUrl(baseUrl, "/api/v1/series/" + pct(it.id) + "/thumbnail");
			out.items.push_back(std::move(ri));
		}
		// no `next`: Komga's paging is numbers, and the caller builds page + 1 with the same request builder
		out.more = page + 1 < totalPages;
		return true;
	}

	bool remoteKomgaChapterList(const std::string& json, int page, std::vector<RemoteChapter>& out, bool& more)
	{
		std::vector<KomgaItem> items;
		int totalPages = 0;
		if (!parseKomgaPage(json, items, totalPages)) return false;
		out.clear();
		for (const KomgaItem& it : items)
		{
			if (it.id.empty()) continue;
			RemoteChapter c;
			c.key = it.id;
			c.name = it.name;
			c.number = komgaNumber(it);
			c.pages = it.pages;	// from the book's media; the reader and the download queue both count on it
			out.push_back(std::move(c));
		}
		more = page + 1 < totalPages;
		return true;
	}

	RemoteRequest remoteOpdsStart(const MangaServer& s, const std::string& password)
	{
		// a Komga server is saved once and offers both faces: its catalogue hangs under the api url at /opds/v1.2,
		// while the other kinds store the OPDS endpoint itself as their url
		std::string url = s.kind == "komga" ? komgaUrl(s.url, "/opds/v1.2") : s.url;
		RemoteRequest r = remoteAuth(s, password, url);
		setAccept(r, "application/atom+xml");	// a feed even from a server whose REST answers are JSON
		return r;
	}

	bool remoteOpdsList(const std::string& feedUrl, const std::string& xml, RemoteList& out)
	{
		OpdsFeed feed;
		if (!parseOpds(xml, feed)) return false;
		out = RemoteList();
		for (const OpdsEntry& e : feed.entries)
		{
			RemoteItem ri;
			ri.title = e.title;
			ri.cover = opdsCover(feedUrl, e);
			std::string nav = e.nav();
			if (!nav.empty())
			{
				// the key is the absolute feed url: opening the folder is remoteAuth(url) and this function again
				ri.key = opdsResolve(feedUrl, nav);
				ri.folder = true;
			}
			else
			{
				// a chapter that surfaced in a mixed feed (a Calibre shelf lists books beside shelves): keyed by
				// where its bytes are, the file when there is one and the stream otherwise
				std::string acq = e.acquisition();
				std::string pse = e.pse();
				const std::string& href = acq.empty() ? pse : acq;
				if (href.empty()) continue;	// neither a way down nor a way to read: nothing a list can open
				ri.key = opdsResolve(feedUrl, href);
			}
			out.items.push_back(std::move(ri));
		}
		if (!feed.next.empty())
		{
			out.next = opdsResolve(feedUrl, feed.next);
			out.more = true;
		}
		return true;
	}

	bool remoteOpdsChapters(const std::string& feedUrl, const std::string& xml, std::vector<RemoteChapter>& out)
	{
		OpdsFeed feed;
		if (!parseOpds(xml, feed)) return false;
		out.clear();
		for (const OpdsEntry& e : feed.entries)
		{
			std::string acq = e.acquisition();
			std::string pse = e.pse();
			if (acq.empty() && pse.empty()) continue;	// a shelf, not a chapter
			int count = pse.empty() ? -1 : pseCount(e, pse);
			RemoteChapter c;
			c.name = e.title;
			c.number = chapterNumber(e.title);	// OPDS carries no ordering field; the name is all there is
			if (count >= 0)
			{
				// streams: the key stays the template with its braces (the feed's own spelling, made absolute);
				// remoteOpdsPageUrls turns it into page urls once the reader knows the panel's width
				c.key = opdsResolve(feedUrl, pse);
				c.pages = count;
				// the whole file, where the server offers both: downloads want it, reading doesn't need it
				if (!acq.empty()) c.url = opdsResolve(feedUrl, acq);
			}
			else if (!acq.empty())
			{
				// a whole CBZ to download and unpack: pages -1 says there are no page urls to ask for
				c.key = opdsResolve(feedUrl, acq);
				c.url = c.key;
			}
			else continue;	// a stream template with no count tells nothing about the pages it holds
			out.push_back(std::move(c));
		}
		return true;
	}

	std::vector<std::string> remoteOpdsPageUrls(const RemoteChapter& ch, int maxWidth)
	{
		std::vector<std::string> urls;
		if (ch.pages <= 0) return urls;	// a whole-file chapter, and an empty stream, both ask for nothing
		urls.reserve((size_t)ch.pages);
		// PSE numbers pages from 0, whatever the reader shows its user; opdsPseUrl fills both brace spellings
		for (int i = 0; i < ch.pages; i++) urls.push_back(opdsPseUrl(ch.key, i, maxWidth));
		return urls;
	}

	RemoteRequest remoteOpdsSearch(const MangaServer& s, const std::string& password, const std::string& descUrl,
	                               const std::string& descXml, const std::string& query)
	{
		std::string templ = searchTemplate(descXml, descUrl);
		if (!templ.empty())
		{
			// the text is the only part this writes: placeholders the server left in the template ({startPage?}
			// and the like) stay as written — they are the server's own contract, not ours to fill
			subAll(templ, "{searchTerms}", pct(query));
			subAll(templ, "%7BsearchTerms%7D", pct(query));
			subAll(templ, "%7bsearchTerms%7d", pct(query));
		}
		RemoteRequest r = remoteAuth(s, password, templ);
		setAccept(r, "application/atom+xml");	// the answer is a feed whatever kind of server this is
		return r;
	}
}
