#pragma once
#include <string>
#include <vector>

namespace bp
{
	// The videos played, most recent first, MAX of them: the browser's Recently watched. history.tsv, a path a line.
	// Apart from resume.tsv: clearing the history leaves the places the videos were left at.
	class PlayHistory
	{
	public:
		static constexpr size_t MAX = 30;

		void parse(const std::string& text);
		std::string serialize() const;
		// first, and only once
		void add(const std::string& path);
		void clear() { paths.clear(); }
		// one taken out (Remove from Recent); false: it wasn't there
		bool remove(const std::string& path);
		// those for which drop is true taken out (the network's: their history switched off)
		void removeIf(bool (*drop)(const std::string&));
		const std::vector<std::string>& items() const { return paths; }

	private:
		std::vector<std::string> paths;
	};
}
