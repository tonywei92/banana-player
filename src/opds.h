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
		long long length = -1;	// the file's size in bytes, when an acquisition link says (Calibre's do)
	};

	// the formats Books' readers open, in the order a book's page offers them: EPUB, then PDF, CBZ, CBR, TXT
	enum class OpdsFormat { Epub, Pdf, Cbz, Cbr, Txt, None };
	// a link's format by its type, else (an octet-stream, a zip) by its file's extension
	OpdsFormat opdsBookFormat(const std::string& type, const std::string& href);
	const char* opdsFormatExt(OpdsFormat f);	// "epub" … ("" for None)
	// the name a page shows for any acquisition's format: "EPUB", "PDF", "MOBI", "AZW3" ("" when nothing says)
	std::string opdsFormatName(const std::string& type, const std::string& href);

	struct OpdsEntry
	{
		std::string id, title, author, summary, updated;
		std::string language, issued;	// dc:language, dc:issued (else Atom's published): a book's facts
		std::vector<std::string> tags;	// its categories' labels (else terms), in order, each once
		std::vector<OpdsLink> links;

		// the href of the link an entry carries for each job, "" when it carries none of that kind
		std::string cover() const;	// rel http://opds-spec.org/image
		std::string thumbnail() const;	// rel http://opds-spec.org/image/thumbnail
		std::string acquisition() const;	// rel http://opds-spec.org/acquisition…, best comicbook zip first
		std::string pse() const;	// rel http://vaemendis.net/opds-pse/stream, still the {pageNumber} template
		std::string nav() const;	// a link down into another catalogue (atom+xml;profile=opds-catalog)
		// Books › Catalogs: the free acquisitions the readers open, one per format, the best first (EPUB, PDF,
		// CBZ, CBR, TXT). Buy, borrow, sample and subscribe links are never offered
		std::vector<OpdsLink> bookOffers() const;
		// every format its acquisition links name ("EPUB", "MOBI"), each once: what a page says it has
		std::vector<std::string> formatNames() const;
		bool isBook() const;	// it carries an acquisition link of any kind
	};

	struct OpdsFeed
	{
		std::string id, title;
		std::vector<OpdsEntry> entries;
		std::string next, search, start, up;	// the feed's own paging links, "" when absent
		std::string searchType;	// the search link's type: an OpenSearch description, or a feed with {searchTerms}
		int total = -1;	// opensearch:totalResults, when the feed says how many there are in all
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
