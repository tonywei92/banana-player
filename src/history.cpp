#include "history.h"
#include <algorithm>
#include <sstream>

namespace bp
{
	void PlayHistory::parse(const std::string& text)
	{
		paths.clear();
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty() || line[0] == '#' || paths.size() >= MAX) continue;
			if (std::find(paths.begin(), paths.end(), line) == paths.end()) paths.push_back(line);
		}
	}

	std::string PlayHistory::serialize() const
	{
		std::string out = "# banana-player: the videos played, most recent first\n";
		for (auto& p : paths) out += p + "\n";
		return out;
	}

	void PlayHistory::add(const std::string& path)
	{
		paths.erase(std::remove(paths.begin(), paths.end(), path), paths.end());
		paths.insert(paths.begin(), path);
		if (paths.size() > MAX) paths.resize(MAX);
	}

	bool PlayHistory::remove(const std::string& path)
	{
		size_t before = paths.size();
		paths.erase(std::remove(paths.begin(), paths.end(), path), paths.end());
		return paths.size() != before;
	}

	void PlayHistory::removeIf(bool (*drop)(const std::string&))
	{
		std::vector<std::string> kept;
		for (auto& p : paths)
			if (!drop(p)) kept.push_back(p);
		paths.swap(kept);
	}
}
