#include "readerfetchplan.h"

namespace bp
{
	std::vector<int> ReaderFetchPlan::next(const State& s)
	{
		std::vector<int> out;
		// inFlight is what is already out: next only fills the room left, so a pass never starts more than max
		int room = s.max - (int)s.inFlight.size();
		if (room <= 0) return out;
		std::set<int> seen;
		for (int i : s.wanted)
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
		std::vector<int> out;
		// a set's order: the same pages are dropped, and logged, in the same order every time
		for (int i : s.inFlight)
			if (!wanted.count(i)) out.push_back(i);
		return out;
	}
}
