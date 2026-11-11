#pragma once
// Keeps a URL chapter's pages coming while the reader has it open (docs/design/manga.md section 7, Preload): up to
// three requests in the reader's own order, reordered when it moves. MangaNet hands it the answers that are its own by
// request id, before the Ui sees them. Main thread only: it tells MangaReader what arrived and registers each finished
// file with the PageCache.
#include "mangafetch.h"
#include "pagecache.h"
#include "reader.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bp
{
	class MangaNet;

	class ReaderFetch
	{
	public:
		// a URL chapter was opened: the fetcher's answers come through net, the reader's files live in cache, whose
		// cap (limit) is applied as each page lands (a direct write bypasses PageCache's own eviction)
		void begin(MangaNet* net, PageCache* cache, MangaReader* reader, const std::vector<std::string>& urls,
			const MangaReader::Headers& headers, uint64_t limit);
		// the reader closed (or another chapter opened): the requests still out are let go
		void end();
		// the fetcher's answer, from MangaNet's routing: true when it is this chapter's page (claimed by id)
		bool own(MangaFetch::Result& result);
		// each pass of main's loop: drop requests for pages no longer wanted, and keep the free slots full
		void pass();

	private:
		void reorder();
		void fill();
		// drop one request: cancel it on the fetch thread, log it, and remember its id so an answer that still
		// arrives is claimed and thrown away instead of reaching the Ui or the decoder
		void cancelOne(int index, uint64_t id, const char* why);

		static constexpr int MAX_IN_FLIGHT = 3;
		MangaNet* net = nullptr;
		PageCache* cache = nullptr;
		MangaReader* reader = nullptr;
		std::vector<std::string> urls;
		MangaReader::Headers headers;
		uint64_t limit = 0;
		std::map<uint64_t, int> byId;	// a request's id -> its page
		std::map<int, uint64_t> inFlight;	// a page -> its request's id
		std::map<uint64_t, uint64_t> started;	// a request's id -> when it went out (the log's ms)
		std::map<uint64_t, int> dropped;	// ids cancelled: an answer that still comes is dropped, not decoded
	};
}
