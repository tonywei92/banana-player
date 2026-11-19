#pragma once
#include <ctime>
#include <string>
#include <vector>

namespace bp
{
	// What was played or read, most recent first: Home's Recent. MAX of each kind (videos, songs, books and documents),
	// so a week of reading doesn't push the films out, nor music the books. history.tsv, a path and when it played
	// (a tab between them; a file an older version wrote has the path alone). Apart from resume.tsv: clearing the
	// history leaves the places the videos were left at.
	class PlayHistory
	{
	public:
		static constexpr size_t MAX = 100;	// of each kind
		static constexpr time_t NO_TIME = 0;	// an entry an older version wrote: no time kept (All recent's Earlier)
		// All recent's groups, by when an entry played
		enum DayGroup { TODAY, YESTERDAY, EARLIER, DAY_GROUPS };
		static const char* dayName(int g);	// "Today", "Yesterday", "Earlier"
		// the group `at` falls in, against `now`; a time of 0 (an old entry) is Earlier. Days are the handheld's own
		static int dayGroup(time_t at, time_t now);
		// a path's kind: 0 a video (or anything else), 1 a song, 2 a book, a document or a comic
		static int kindOf(const std::string& path);

		void parse(const std::string& text);
		std::string serialize() const;
		// first, and only once. at: when it played (time(nullptr)); NO_TIME for an entry whose time isn't known
		void add(const std::string& path, time_t at = NO_TIME);
		void clear() { paths.clear(); ats.clear(); }
		// one taken out (Remove from Recent); false: it wasn't there
		bool remove(const std::string& path);
		// those for which drop is true taken out (the network's: their history switched off)
		void removeIf(bool (*drop)(const std::string&));
		// a file or folder moved (browser.h's movedPath): its entries, and those inside it, follow; true if any did
		bool moved(const std::string& from, const std::string& to);
		const std::vector<std::string>& items() const { return paths; }
		// when each item played, in the same order (NO_TIME: not known)
		const std::vector<time_t>& times() const { return ats; }
		time_t timeOf(const std::string& path) const;

	private:
		std::vector<std::string> paths;
		std::vector<time_t> ats;	// one for each path
		void trim();	// each kind cut to its newest MAX
	};
}
