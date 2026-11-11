// A source series' last details and chapter list on the card (docs/design/manga.md 6: <state>/manga/series/<hash>.json).
// Its page opens from here at once, and only Refresh asks the source again. Window-free: the file's name, its JSON and
// how old a check reads, so the round trip is tested on its own (tests/core_test.cpp).
#pragma once
#include "mangasource.h"
#include <cstdint>
#include <string>
#include <vector>

namespace bp
{
	// What one series' cache holds: the details and chapters as last fetched, and when. The chapters keep the script's
	// own table (json) so a later pages() gets back what it gave (5.2).
	struct MangaSeriesCache
	{
		MangaDetails details;
		std::vector<MangaChapter> chapters;
		int64_t fetchedAt = 0;	// seconds since the epoch; 0 when the file held no time
		bool detailsOn = false, chaptersOn = false;	// the fetch's two halves are here (a failed one is left out)
	};

	// <stateDir>/manga/series/<sha256 of the series id>.json. Never the id or its URL as a name: a URL has '/' and
	// ':' and can be long. "" when there is no state folder or the digest fails.
	std::string mangaSeriesCachePath(const std::string& stateDir, const std::string& seriesId);

	// The file's text: the details, the chapters and the time they were fetched. Written with json.h's writer, so a
	// title or description holding quotes and control characters still reads back.
	std::string mangaSeriesCacheJson(const MangaSeriesCache& c);

	// False for text that isn't this file's shape (a damaged file is no cache, never an error to show).
	bool mangaSeriesCacheParse(const std::string& text, MangaSeriesCache& out);

	// The file below the state folder; false when there is nowhere to read or the text isn't a cache. A missing,
	// damaged or unreadable file is simply no cache.
	bool mangaSeriesCacheRead(const std::string& stateDir, const std::string& seriesId, MangaSeriesCache& out);
	// The folders are made as needed and the file is written beside and renamed over it, as the other stores do.
	bool mangaSeriesCacheWrite(const std::string& stateDir, const std::string& seriesId, const MangaSeriesCache& c);

	// How old a check reads, in the page's own words: "today", "1 day", "N days"; "" with no time (0). A clock behind
	// the file's stamp counts as today.
	std::string mangaCheckedAgo(int64_t fetchedAt, int64_t now);

	// More than a day old: the page then says when it was checked and offers L2 Refresh (never automatically).
	bool mangaCheckedStale(int64_t fetchedAt, int64_t now);
}
