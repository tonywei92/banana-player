// The media index's tag cache, <state>/media-index2.tsv: what the Music folders' files are called (their tags), kept
// across runs and keyed by each file's path, size and mtime, so a second start reads only what changed (a walk
// without it would open every song on the card again). One line a file, appended as the walk learns it — a quit
// mid-pass keeps what was read — the last line a path wins, and the file is compacted at load when it has grown well
// past what it holds: durations.txt's shape.
//
// No mutex, and no cap: one pass's cache belongs to that one walk thread alone (the worker makes it on its thread,
// uses it, drops it), and its entries are what the card holds, not a window onto it.
#pragma once
#include "mediatags.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace bp
{
	class MediaTagCache
	{
	public:
		~MediaTagCache();
		// The state folder's media-index2.tsv, kept across runs. False: the file couldn't be opened for appending
		// (the walk goes on; nothing it learns is kept).
		bool load(const std::string& file);
		// true when the file as it is now is already known — its path, size and mtime match an entry: its tags are
		// copied to out. A file rewritten (another size or mtime) is a miss, so nothing goes stale.
		bool find(const std::string& path, long long size, int64_t mtime, MediaTags& out) const;
		// the file's tags as they are now. video says what the record is — the player opens the file as a video by
		// its name — not what the tags hold. Also the answer for a file that couldn't be opened at all (a stable
		// answer: it is not read again next pass).
		void put(const std::string& path, long long size, int64_t mtime, const MediaTags& tags, bool video);
		size_t size() const { return entries.size(); }

	private:
		// a line's words are pooled once more: an album's songs share their artist and album, and the cache lives
		// only for the walk, so its pool is dropped with it
		struct Entry
		{
			long long size = 0;
			int64_t mtime = 0;
			bool video = false;
			int track = 0, disc = 0, year = 0;
			int duration = -1;		// seconds; -1 unknown (an int: no song's row needs finer)
			uint32_t title = 0, artist = 0, albumArtist = 0, album = 0;
		};
		uint32_t word(const std::string& s);
		std::string line(const std::string& path, const Entry& e) const;
		std::vector<std::string> words;
		std::unordered_map<std::string, uint32_t> where;
		std::unordered_map<std::string, Entry> entries;
		FILE* out = nullptr;	// the append stream; null before load() or when it couldn't be opened
	};
}
