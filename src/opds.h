#pragma once
// What the comic servers answer with, read as text (docs/design/manga.md, section 5.1): an OPDS 1.2 Atom feed (Kavita
// and Komga's OPDS, Calibre and the like) and one page of a Komga REST list. Strings in, structs out: no network, no
// threads, no SDL, so core-test can feed these captured answers, and a response we can't read is a false, never a
// crash, as with comicinfo.
#include <string>
#include <vector>

namespace bp
{
	struct OpdsLink
	{
		std::string rel, type, href, title;
		int count = -1;	// thr:count or pse:count carried by the link; -1 when it carries none
	};

	struct OpdsEntry
	{
		std::string id, title, author, summary, updated;
		std::vector<OpdsLink> links;

		// the href of the link an entry carries for each job, "" when it carries none of that kind
		std::string cover() const;	// rel http://opds-spec.org/image
		std::string thumbnail() const;	// rel http://opds-spec.org/image/thumbnail
		std::string acquisition() const;	// rel http://opds-spec.org/acquisition…, best comicbook zip first
		std::string pse() const;	// rel http://vaemendis.net/opds-pse/stream, still the {pageNumber} template
		std::string nav() const;	// a link down into another catalogue (atom+xml;profile=opds-catalog)
	};

	struct OpdsFeed
	{
		std::string id, title;
		std::vector<OpdsEntry> entries;
		std::string next, search, start, up;	// the feed's own paging links, "" when absent
	};

	// an Atom feed the way the OPDS servers write it: namespaces are matched by URI whatever their prefix, entities and
	// CDATA are decoded, and a summary or html content comes out as one line of text. Input over 8 MB is refused, and
	// a feed keeps at most 10 000 entries. false (out unchanged) when the text isn't a feed that closes properly.
	bool parseOpds(const std::string& xml, OpdsFeed& out);

	// a link's href against the URL of the feed it came from: absolute, "//host/x", root-relative "/x", relative "x",
	// "../x" and query-only "?a=1" all resolve
	std::string opdsResolve(const std::string& base, const std::string& href);

	// a PSE template for one page: fills {pageNumber} and {maxWidth}, also where a server wrote them %7B…%7D
	std::string opdsPseUrl(const std::string& templ, int page, int maxWidth);

	struct KomgaItem
	{
		std::string id, name, number;	// a series carries no number; it stays text, Komga writes "10.5" or "Extra"
		int pages = -1;			// a book's media page count
		double numberSort = -1;		// Komga's own ordering value
	};

	// one page of a Komga list answer ({"content": [...], "totalPages": n}) of series or books. Fields we don't use
	// are ignored; malformed JSON is false; items append up to the same 10 000-entry cap as a feed.
	bool parseKomgaPage(const std::string& json, std::vector<KomgaItem>& items, int& totalPages);

	// base + path joined with exactly one slash, whichever way the two happened to be written
	std::string komgaUrl(const std::string& base, const std::string& path);
}
