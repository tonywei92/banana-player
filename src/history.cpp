#include "history.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <sstream>

namespace bp
{
	void PlayHistory::parse(const std::string& text)
	{
		paths.clear();
		times.clear();
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			// a file's time, written after its path. An older build reads it as a comment and keeps the path alone
			if (line.rfind("#time\t", 0) == 0)
			{
				size_t t = line.find('\t', 6);
				if (t == std::string::npos) continue;
				std::string path = line.substr(t + 1);
				auto it = std::find(paths.begin(), paths.end(), path);
				if (it != paths.end()) times[it - paths.begin()] = atoll(line.substr(6, t - 6).c_str());
				continue;
			}
			if (line.empty() || line[0] == '#' || paths.size() >= MAX) continue;
			if (std::find(paths.begin(), paths.end(), line) == paths.end())
			{
				paths.push_back(line);
				times.push_back(0);
			}
		}
	}

	std::string PlayHistory::serialize() const
	{
		std::string out = "# banana-player: the videos played, most recent first\n";
		for (size_t i = 0; i < paths.size(); i++)
		{
			out += paths[i] + "\n";
			if (i < times.size() && times[i] > 0) out += "#time\t" + std::to_string(times[i]) + "\t" + paths[i] + "\n";
		}
		return out;
	}

	void PlayHistory::add(const std::string& path)
	{
		auto it = std::find(paths.begin(), paths.end(), path);
		if (it != paths.end())
		{
			size_t i = it - paths.begin();
			paths.erase(it);
			times.erase(times.begin() + i);
		}
		paths.insert(paths.begin(), path);
		times.insert(times.begin(), (int64_t)time(nullptr));
		if (paths.size() > MAX)
		{
			paths.resize(MAX);
			times.resize(MAX);
		}
	}

	bool PlayHistory::remove(const std::string& path)
	{
		auto it = std::find(paths.begin(), paths.end(), path);
		if (it == paths.end()) return false;
		size_t i = it - paths.begin();
		paths.erase(it);
		times.erase(times.begin() + i);
		return true;
	}

	void PlayHistory::removeIf(bool (*drop)(const std::string&))
	{
		std::vector<std::string> kept;
		std::vector<int64_t> keptTimes;
		for (size_t i = 0; i < paths.size(); i++)
			if (!drop(paths[i]))
			{
				kept.push_back(paths[i]);
				keptTimes.push_back(i < times.size() ? times[i] : 0);
			}
		paths.swap(kept);
		times.swap(keptTimes);
	}

	int64_t PlayHistory::timeOf(const std::string& path) const
	{
		auto it = std::find(paths.begin(), paths.end(), path);
		return it == paths.end() || (size_t)(it - paths.begin()) >= times.size() ? 0 : times[it - paths.begin()];
	}
}
