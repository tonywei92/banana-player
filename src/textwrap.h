#pragma once
// Where a too-long line breaks (pure, so Ui::wrapText, the top pane's wrap and the tests share it). A name with no
// spaces ("edgar-rice-burroughs_a-fighting-man-of-mars.epub") breaks after its - _ . / before it breaks mid-word.
#include <algorithm>
#include <string>
#include <vector>

namespace textwrap
{
	// `fit` bytes of `rest` fit a line (a character boundary); the byte length the line should take, the next one
	// starting there: after a space (taken with it) when one lies past the half, else after a - _ . / past the
	// quarter, else at `fit` itself
	inline size_t breakAt(const std::string& rest, size_t fit)
	{
		if (fit >= rest.size()) return rest.size();
		size_t sp = rest.find_last_of(' ', fit);
		if (sp != std::string::npos && sp > fit / 2) return sp + 1;
		for (size_t p = fit; p > fit / 4 && p > 0; p--)
		{
			char c = rest[p - 1];
			// a dot before a digit ("3.5") and a dash before a space stay put
			if ((c == '-' || c == '_' || c == '.' || c == '/') && p < rest.size() && rest[p] != ' ') return p;
		}
		return fit;
	}

	// `text` in lines of at most `maxLines`; `fits(s)` says whether s is no wider than a line. The last line holds
	// what is left, cut to 400 bytes (the caller draws it with "..."). Binary search over character starts: measuring
	// every prefix of a long text, on every redraw, took a quarter of a second on the handheld.
	template <class Fits>
	std::vector<std::string> lines(const std::string& text, int maxLines, Fits fits)
	{
		std::vector<std::string> out;
		std::string rest = text;
		while (!rest.empty() && (int)out.size() < maxLines)
		{
			if ((int)out.size() == maxLines - 1)
			{
				out.push_back(rest.size() > 400 ? rest.substr(0, 400) : rest);
				break;
			}
			if (fits(rest)) { out.push_back(rest); break; }
			std::vector<size_t> ends;	// byte length of each prefix of 1, 2, ... characters
			for (size_t i = 1; i <= rest.size(); i++)
				if (i == rest.size() || ((unsigned char)rest[i] & 0xC0) != 0x80) ends.push_back(i);
			size_t lo = 0, hi = ends.size();	// prefixes of lo characters fit (0 trivially), of hi don't
			while (hi - lo > 1)
			{
				size_t mid = (lo + hi) / 2;
				if (fits(rest.substr(0, ends[mid - 1]))) lo = mid;
				else hi = mid;
			}
			size_t fit = lo ? ends[lo - 1] : 0;
			if (fit == 0) fit = ends.empty() ? rest.size() : ends[0];
			fit = breakAt(rest, fit);
			std::string line = rest.substr(0, fit);
			while (!line.empty() && line.back() == ' ') line.pop_back();
			out.push_back(line);
			rest = rest.substr(fit);
		}
		return out;
	}
}
