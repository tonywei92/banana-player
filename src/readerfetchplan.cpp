#include "readerfetchplan.h"
#include <algorithm>

namespace bp
{
	SeekRun::Now SeekRun::seek(int page, int64_t now)
	{
		// the first Seek of a run, or one after the run rested: a tap, and the view follows at once
		if (this->page < 0 || now - at >= SETTLE_MS)
		{
			this->page = page;
			count = 1;
			at = now;
			held = false;
			return Now::Commit;
		}
		// another within the window: a drag. the run's target is the last one, held until the seeks rest
		this->page = page;
		count++;
		at = now;
		held = true;
		return Now::Hold;
	}

	bool SeekRun::due(int64_t now) const
	{
		return held && page >= 0 && now - at >= SETTLE_MS;
	}

	void SeekRun::commit()
	{
		page = -1;
		count = 0;
		at = 0;
		held = false;
	}

	void SeekRun::reset() { commit(); }

	std::vector<int> ReaderFetchPlan::ordered(const std::vector<int>& wanted, int target, bool targetIn,
		int64_t targetAt, int64_t now)
	{
		// a page not in the wanted list (a jump clamped elsewhere, or a stale target) never gates the list
		if (target < 0 || targetIn || now - targetAt >= TARGET_GRACE_MS) return wanted;
		if (std::find(wanted.begin(), wanted.end(), target) == wanted.end()) return wanted;
		// the target alone: the page asked for is never behind its own preload
		return {target};
	}

	std::vector<int> ReaderFetchPlan::next(const State& s)
	{
		std::vector<int> out;
		// inFlight is what is already out: next only fills the room left, so a pass never starts more than max
		int room = s.max - (int)s.inFlight.size();
		if (room <= 0) return out;
		std::set<int> seen;
		for (int i : ordered(s.wanted, s.target, s.targetIn, s.targetAt, s.now))
		{
			if (i < 0) continue;
			// the Strip's wanted list can repeat a page in view: one request, not two
			if (!seen.insert(i).second) continue;
			if (s.done.count(i) || s.inFlight.count(i) || s.failed.count(i)) continue;
			out.push_back(i);
			if ((int)out.size() >= room) break;
		}
		return out;
	}

	std::vector<int> ReaderFetchPlan::stale(const State& s)
	{
		std::set<int> wanted(s.wanted.begin(), s.wanted.end());
		// a jump's target goes alone: while it is not in, a neighbour already out would race it for the page
		bool alone = s.target >= 0 && !s.targetIn && s.now - s.targetAt < TARGET_GRACE_MS && wanted.count(s.target) > 0;
		std::vector<int> out;
		// a set's order: the same pages are dropped, and logged, in the same order every time
		for (int i : s.inFlight)
			if (alone ? i != s.target : !wanted.count(i)) out.push_back(i);
		return out;
	}
}
