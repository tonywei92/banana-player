#include "bookplace.h"

namespace bp
{
	bool samePlace(const ResumeEntry& a, const ResumeEntry& b)
	{
		return a.aid == b.aid && (int64_t)a.position == (int64_t)b.position;
	}

	std::string placeWriter(const std::string& by)
	{
		std::string s;
		for (char c : by) s += c == '\t' || c == '\n' || c == '\r' ? ' ' : c;
		if (s.size() > 40)
		{
			size_t cut = 40;
			while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
			s.resize(cut);
		}
		return s.empty() ? "Browser" : s;
	}

	PlaceVerdict mergePlace(const ResumeEntry* stored, const PlaceWrite& w, ResumeEntry& out)
	{
		ResumeEntry e;
		e.path = w.path;
		e.aid = w.kind;
		e.position = w.position;
		e.duration = w.share;
		e.by = placeWriter(w.by);
		e.when = w.when;
		uint64_t rev = stored ? stored->rev : 0;
		e.rev = rev + 1;
		if (!stored || w.force || stored->rev == w.base)
		{
			out = e;
			return PlaceVerdict::Written;
		}
		out = *stored;
		return samePlace(*stored, e) ? PlaceVerdict::Same : PlaceVerdict::Conflict;
	}

	ResumeEntry stampPlace(const ResumeEntry* stored, ResumeEntry e, const std::string& by, int64_t when)
	{
		e.rev = (stored ? stored->rev : 0) + 1;
		e.by = placeWriter(by);
		e.when = when;
		return e;
	}
}
