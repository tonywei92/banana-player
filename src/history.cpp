#include "history.h"
#include "browser.h"
#include "pagebook.h"
#include "playlist.h"
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
			if (line.empty() || line[0] == '#') continue;
			if (std::find(paths.begin(), paths.end(), line) == paths.end()) paths.push_back(line);
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
		kept.reserve(paths.size());
		for (auto& p : paths)
			if (n[kindOf(p)]++ < MAX) kept.push_back(p);
		paths = std::move(kept);
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
		trim();
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
}
