#pragma once
// Which of a URL chapter's pages to fetch now, and which requests to let go when the reader moves (docs/design/manga.md
// section 7, Preload): the reader's wanted order, the pages already on the card, the requests in flight and the ones
// that failed. Pure (no thread, no socket) so the reordering is tested without a server; the reader uses it to hand
// pages out, and main's fetcher to decide what to cancel.
#include <cstdint>
#include <set>
#include <vector>

namespace bp
{
	// The reader's Seek rule: a Seek moves the view at once, but want() and the fetch plan follow the view only when
	// the seeks stop. The first Seek of a run goes at once (a tap on the slider, the grid's A); a second within the
	// settle window marks a drag, and the run's last target goes once none has come for SETTLE_MS. Pure, so the rule
	// is tested without the reader (tests/core_test.cpp).
	class SeekRun
	{
	public:
		static constexpr int64_t SETTLE_MS = 150;
		enum class Now { Commit, Hold };

		// a Seek arrived at `now` for `page`: Commit when it is the run's first (the reader wants it at once),
		// Hold when it is part of a run still moving (the reader waits for due()).
		Now seek(int page, int64_t now);
		// a held run has rested: the reader commits target() now, then commit()s it
		bool due(int64_t now) const;
		bool pending() const { return held; }
		int target() const { return page; }
		int seeks() const { return count; }
		void commit();
		void reset();

	private:
		int page = -1;
		int count = 0;
		int64_t at = 0;	// when the last Seek of the run came
		bool held = false;
	};

	class ReaderFetchPlan
	{
	public:
		// a jump's target waits for its page (fetched for a URL chapter, decoded) alone this long before the
		// preload around it may start
		static constexpr int64_t TARGET_GRACE_MS = 2000;

		struct State
		{
			std::vector<int> wanted;	// nearest first, as the reader lists them (may repeat a page)
			std::set<int> done;			// the page's file is already in the cache
			std::set<int> inFlight;		// a request is out for it
			std::set<int> failed;		// it failed since it was last wanted
			int max = 3;				// how many requests may be out at once
			// a jump's target: ordered first, alone, until it is in (or failed), or the grace has passed
			int target = -1;
			bool targetIn = false;
			int64_t targetAt = 0;
			int64_t now = 0;
		};

		// The wanted list as a jump wants it: only the target until targetIn or TARGET_GRACE_MS has passed
		// since targetAt, then the whole list untouched.
		static std::vector<int> ordered(const std::vector<int>& wanted, int target, bool targetIn,
			int64_t targetAt, int64_t now);
		// The pages to start now: missing, not in flight, not failed, in wanted order, no more than the free room.
		static std::vector<int> next(const State& s);
		// The requests to drop: in flight for a page no longer wanted at all (the reader moved away).
		static std::vector<int> stale(const State& s);
	};
}
