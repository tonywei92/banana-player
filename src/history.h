#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bp
{
	// The videos played, most recent first, MAX of them: the browser's Recently watched. history.tsv, a path a line
	// and (since the manga reader's card shares Home's strip) a #time line after it. Apart from resume.tsv: clearing
	// the history leaves the places the videos were left at.
	class PlayHistory
	{
	public:
		static constexpr size_t MAX = 30;

		void parse(const std::string& text);
		std::string serialize() const;
		// first, and only once
		void add(const std::string& path);
		void clear() { paths.clear(); times.clear(); }
		// one taken out (Remove from Recent); false: it wasn't there
		bool remove(const std::string& path);
		// those for which drop is true taken out (the network's: their history switched off)
		void removeIf(bool (*drop)(const std::string&));
		const std::vector<std::string>& items() const { return paths; }
		// when it was last played (seconds since 1970), 0 for a file kept before times were: Home's order with manga
		int64_t timeOf(const std::string& path) const;

	private:
		std::vector<std::string> paths;
		std::vector<int64_t> times;	// (the same places as paths)
	};
}
