#pragma once
// Which of a URL chapter's pages to fetch now, and which requests to let go when the reader moves (docs/design/manga.md
// section 7, Preload): the reader's wanted order, the pages already on the card, the requests in flight and the ones
// that failed. Pure (no thread, no socket) so the reordering is tested without a server; the reader uses it to hand
// pages out, and main's fetcher to decide what to cancel.
#include <set>
#include <vector>

namespace bp
{
	class ReaderFetchPlan
	{
	public:
		struct State
		{
			std::vector<int> wanted;	// nearest first, as the reader lists them (may repeat a page)
			std::set<int> done;			// the page's file is already in the cache
			std::set<int> inFlight;		// a request is out for it
			std::set<int> failed;		// it failed since it was last wanted
			int max = 3;				// how many requests may be out at once
		};

		// The pages to start now: missing, not in flight, not failed, in wanted order, no more than the free room.
		static std::vector<int> next(const State& s);
		// The requests to drop: in flight for a page no longer wanted at all (the reader moved away).
		static std::vector<int> stale(const State& s);
	};
}
