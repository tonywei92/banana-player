#include "history.h"
#include "browser.h"
#include "pagebook.h"
#include "playlist.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace bp
{
	namespace
	{
		// a civil day's number, from its date (Howard Hinnant's days_from_civil): days never depend on the clock's
		// DST shifts, so yesterday is yesterday after a change of hour too
		long long dayNumber(int y, int m, int d)
		{
			y -= m <= 2;
			const long long era = (y >= 0 ? y : y - 399) / 400;
			const long long yoe = y - era * 400;
			const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
			const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
			return era * 146097 + doe - 719468;
		}
		long long dayOf(time_t t)
		{
			struct tm tmv;
			if (!localtime_r(&t, &tmv)) return 0;
			return dayNumber(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
		}
	}

	const char* PlayHistory::dayName(int g)
	{
		switch (g)
		{
		case TODAY: return "Today";
		case YESTERDAY: return "Yesterday";
		default: return "Earlier";
		}
	}

	int PlayHistory::dayGroup(time_t at, time_t now)
	{
		if (at == NO_TIME) return EARLIER;
		long long d = dayOf(at), n = dayOf(now);
		if (d >= n) return TODAY;
		if (d == n - 1) return YESTERDAY;
		return EARLIER;
	}

	void PlayHistory::parse(const std::string& text)
	{
		paths.clear();
		ats.clear();
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty() || line[0] == '#') continue;
			// a path, then (a tab and) when it played; an older file has the path alone
			std::string path = line, at;
			size_t tab = line.find('\t');
			if (tab != std::string::npos)
			{
				path = line.substr(0, tab);
				at = line.substr(tab + 1);
			}
			if (path.empty() || std::find(paths.begin(), paths.end(), path) != paths.end()) continue;
			paths.push_back(path);
			ats.push_back(at.empty() ? NO_TIME : (time_t)std::strtoll(at.c_str(), nullptr, 10));
		}
		trim();
	}

	int PlayHistory::kindOf(const std::string& path)
	{
		if (isDocumentName(path) || isComicName(path)) return 2;
		return isAudioFile(path) ? 1 : 0;
	}

	void PlayHistory::trim()
	{
		size_t n[3] = {0, 0, 0};
		std::vector<std::string> kept;
		std::vector<time_t> keptAt;
		kept.reserve(paths.size());
		keptAt.reserve(paths.size());
		for (size_t i = 0; i < paths.size(); i++)
		{
			auto& p = paths[i];
			if (n[kindOf(p)]++ < MAX)
			{
				kept.push_back(p);
				keptAt.push_back(i < ats.size() ? ats[i] : NO_TIME);
			}
		}
		paths = std::move(kept);
		ats = std::move(keptAt);
	}

	std::string PlayHistory::serialize() const
	{
		// the time is the tab and when it played: a file an older version wrote is still read (the path alone)
		std::string out = "# banana-player: the videos played, most recent first (path, then when)\n";
		for (size_t i = 0; i < paths.size(); i++)
			out += paths[i] + (i < ats.size() && ats[i] != NO_TIME ? "\t" + std::to_string((long long)ats[i]) : "") + "\n";
		return out;
	}

	void PlayHistory::add(const std::string& path, time_t at)
	{
		for (size_t i = 0; i < paths.size(); i++)
			if (paths[i] == path)
			{
				paths.erase(paths.begin() + i);
				if (i < ats.size()) ats.erase(ats.begin() + i);
				break;
			}
		paths.insert(paths.begin(), path);
		ats.insert(ats.begin(), at);
		trim();
	}

	bool PlayHistory::remove(const std::string& path)
	{
		bool any = false;
		for (size_t i = 0; i < paths.size();)
			if (paths[i] == path)
			{
				paths.erase(paths.begin() + i);
				if (i < ats.size()) ats.erase(ats.begin() + i);
				any = true;
			}
			else i++;
		return any;
	}

	void PlayHistory::removeIf(bool (*drop)(const std::string&))
	{
		std::vector<std::string> kept;
		std::vector<time_t> keptAt;
		for (size_t i = 0; i < paths.size(); i++)
			if (!drop(paths[i]))
			{
				kept.push_back(paths[i]);
				keptAt.push_back(i < ats.size() ? ats[i] : NO_TIME);
			}
		paths.swap(kept);
		ats.swap(keptAt);
	}

	bool PlayHistory::moved(const std::string& from, const std::string& to)
	{
		bool any = false;
		for (auto& p : paths)
		{
			std::string n;
			if (movedPath(p, from, to, n)) { p = n; any = true; }
		}
		return any;
	}

	time_t PlayHistory::timeOf(const std::string& path) const
	{
		for (size_t i = 0; i < paths.size(); i++)
			if (paths[i] == path) return i < ats.size() ? ats[i] : NO_TIME;
		return NO_TIME;
	}
}
