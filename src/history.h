#pragma once
#include <string>
#include <vector>

namespace bp
{
	// What was played or read, most recent first: Home's Recent. MAX of each kind (videos, songs, books and documents),
	// so a week of reading doesn't push the films out, nor music the books. history.tsv, a path a line.
	// Apart from resume.tsv: clearing the history leaves the places the videos were left at.
	class PlayHistory
	{
	public:
		static constexpr size_t MAX = 30;	// of each kind
		// a path's kind: 0 a video (or anything else), 1 a song, 2 a book, a document or a comic
		static int kindOf(const std::string& path);

		void parse(const std::string& text);
		std::string serialize() const;
		// first, and only once
		void add(const std::string& path);
		void clear() { paths.clear(); }
		// one taken out (Remove from Recent); false: it wasn't there
		bool remove(const std::string& path);
		// those for which drop is true taken out (the network's: their history switched off)
		void removeIf(bool (*drop)(const std::string&));
		// a file or folder moved (browser.h's movedPath): its entries, and those inside it, follow; true if any did
		bool moved(const std::string& from, const std::string& to);
		const std::vector<std::string>& items() const { return paths; }

	private:
		std::vector<std::string> paths;
		void trim();	// each kind cut to its newest MAX
	};
}
