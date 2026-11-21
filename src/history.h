#pragma once
#include <ctime>
#include <string>
#include <vector>

namespace bp
{
	// The list a song of Recent played from (songs only; docs/notes/2026-11-21-recent-context.md): opening the song
	// again plays it in that list. A playlist by its name (the file's, less ".m3u": never the path), a Files folder by
	// its path; a list of the Music rail (an album, an artist, a Music folder, Play random, with what Keep playing
	// added) by the songs as they played, since an album's own order or a random draw can't be read again
	struct RecentContext
	{
		enum Kind { NONE, PLAYLIST, ALBUM, ARTIST, MUSIC_FOLDER, RANDOM, FOLDER };
		Kind kind = NONE;
		// PLAYLIST: its name; ALBUM, ARTIST: theirs as the player titled the list; MUSIC_FOLDER, FOLDER: the folder's
		// path; RANDOM: the list's title ("Random")
		std::string name;
		std::vector<std::string> list;	// the rail's kinds: the list as it played (else empty)
		int index = -1;	// the song's place in the list that played (a song listed twice: this one)
		bool rail() const { return kind == ALBUM || kind == ARTIST || kind == MUSIC_FOLDER || kind == RANDOM; }
		// the list's title, as the Session's source had it ("Album \xC2\xB7 Blue"); FOLDER, PLAYLIST: name's last part
		std::string title() const;
		bool operator==(const RecentContext& o) const { return kind == o.kind && name == o.name && list == o.list && index == o.index; }
		bool operator!=(const RecentContext& o) const { return !(*this == o); }
		static constexpr size_t LIST_MAX = 400;	// a rail list kept to this many around the song (Keep playing grows it)
	};
	// the context of `song` from what the Session plays: its queue and index, its source (kind 0 folder, 1 network
	// folder, 2 playlist; title; id: a playlist's name) and rail. NONE for a list of no folder (a selection)
	RecentContext recentContextOf(const std::string& song, const std::vector<std::string>& queue, int index,
		int sourceKind, const std::string& title, const std::string& sourceId, bool rail);

	// why a file of Recent can't open: "" it's there. A card that can be taken out (its mount point and its name)
	struct RemovableCard
	{
		std::string path, name;	// "/run/sd2", "Card 2"
		bool in = false;
	};
	std::string recentMissing(const std::string& path, bool there, const std::vector<RemovableCard>& cards);

	// What was played or read, most recent first: Home's Recent. MAX of each kind (videos, songs, books and documents),
	// so a week of reading doesn't push the films out, nor music the books. history.tsv, a path and when it played
	// (a tab between them; a file an older version wrote has the path alone), then a song's RecentContext as more
	// tab-separated fields (kind, name, list id, index) an older version doesn't read; the rail's lists are lines of
	// their own ("#list", its id, its paths), which an older version skips as comments. Apart from resume.tsv: clearing the
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
		// (a new play: its context is none until setContext says where it played from)
		void add(const std::string& path, time_t at = NO_TIME);
		void clear() { paths.clear(); ats.clear(); ctxs.clear(); }
		// where a song in the history played from; true when that changed (a rail list that grows by Keep playing
		// takes the place of the shorter one it began as, for every song that played from it)
		bool setContext(const std::string& path, RecentContext ctx);
		const RecentContext& contextOf(const std::string& path) const;
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
		std::vector<RecentContext> ctxs;	// one for each path
		void eraseAt(size_t i);
		void trim();	// each kind cut to its newest MAX
	};
}
