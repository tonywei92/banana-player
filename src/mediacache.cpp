// The media index's tag cache (see mediacache.h). The line is
//   path TAB size TAB mtime TAB kind TAB track TAB disc TAB year TAB duration TAB title TAB artist TAB albumArtist TAB album
// kind "s" a song, "v" a music video. A path holds a tab or a newline: the entry is skipped (it cannot sit on one
// line); a word that holds one has it turned into a space (tags are read for search, and neither separator says
// anything there). A malformed line, and a torn last one, are dropped, so a card lost power mid-write only loses the
// line being written.
#include "mediacache.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <unistd.h>

namespace bp
{
	namespace
	{
		// the words as they can sit in a line: no tabs, no newlines (a folded word would fold them away anyway)
		std::string clean(std::string s)
		{
			for (char& c : s)
				if (c == '\t' || c == '\n' || c == '\r') c = ' ';
			return s;
		}

		bool number(const std::string& s, long long& v)
		{
			if (s.empty()) return false;
			errno = 0;
			char* end = nullptr;
			v = strtoll(s.c_str(), &end, 10);
			return errno == 0 && end && *end == '\0';
		}

		const size_t FIELDS = 12;	// path, size, mtime, kind, track, disc, year, duration, title, artist, albumArtist, album
	}

	MediaTagCache::~MediaTagCache()
	{
		if (out) fclose(out);
	}

	uint32_t MediaTagCache::word(const std::string& s)
	{
		auto it = where.find(s);
		if (it != where.end()) return it->second;
		words.push_back(s);
		where[s] = (uint32_t)(words.size() - 1);
		return (uint32_t)(words.size() - 1);
	}

	std::string MediaTagCache::line(const std::string& path, const Entry& e) const
	{
		// (plain concatenation, never a snprintf: a long card path truncates in one, and the line would read back
		// as one with its tail fields glued together)
		std::string s = path;
		s += '\t' + std::to_string(e.size) + '\t' + std::to_string(e.mtime) + '\t';
		s += e.video ? 'v' : 's';
		s += '\t' + std::to_string(e.track) + '\t' + std::to_string(e.disc) + '\t' + std::to_string(e.year) + '\t' +
			std::to_string(e.duration);
		s += '\t' + words[e.title] + '\t' + words[e.artist] + '\t' + words[e.albumArtist] + '\t' + words[e.album] + '\n';
		return s;
	}

	bool MediaTagCache::load(const std::string& file)
	{
		if (out)
		{
			fclose(out);
			out = nullptr;
		}
		// a load replaces what was known: the file is the whole answer
		words.clear();
		words.emplace_back();	// (index 0: the empty word)
		where.clear();
		where[""] = 0;
		entries.clear();
		size_t lines = 0;
		bool discarding = false;
		if (FILE* in = fopen(file.c_str(), "r"))
		{
			std::string line;
			char buf[4096];
			while (fgets(buf, sizeof buf, in))
			{
				line += buf;
				if (line.empty() || line.back() != '\n')
				{
					// no tag list is this long: malformed, and the rest of it goes with the newline
					if (line.size() > 16384)
					{
						discarding = true;
						line.clear();
					}
					continue;
				}
				lines++;
				if (discarding)
				{
					discarding = false;
					line.clear();
					continue;
				}
				line.pop_back();
				// the fields, by tab; a line that doesn't hold exactly the twelve is a word with a tab or a tear
				std::vector<size_t> at;
				for (size_t t = line.find('\t'); t != std::string::npos; t = line.find('\t', t + 1)) at.push_back(t);
				if (at.size() == FIELDS - 1 && at[0] > 0)
				{
					// the last field runs to the line's end (a word may be empty: a tag that isn't there)
					auto field = [&](size_t i) {
						size_t a = i == 0 ? 0 : at[i - 1] + 1, b = i < at.size() ? at[i] : line.size();
						return line.substr(a, b - a);
					};
					Entry e;
					long long size = 0, mtime = 0, track = 0, disc = 0, year = 0, duration = 0;
					std::string kind = field(3);
					if (number(field(1), size) && number(field(2), mtime) && number(field(4), track) &&
						number(field(5), disc) && number(field(6), year) && number(field(7), duration) &&
						(kind == "s" || kind == "v"))
					{
						e.size = size;
						e.mtime = mtime;
						e.video = kind == "v";
						e.track = (int)std::min<long long>(track, INT32_MAX);
						e.disc = (int)std::min<long long>(disc, INT32_MAX);
						e.year = (int)std::min<long long>(year, INT32_MAX);
						e.duration = (int)std::min<long long>(duration, INT32_MAX);
						e.title = word(field(8));
						e.artist = word(field(9));
						e.albumArtist = word(field(10));
						e.album = word(field(11));
						// the last line a path wins, as durations' cache
						entries[field(0)] = e;
					}
				}
				line.clear();
			}
			// a last line with no newline was cut short (the card lost power while it was written): not an entry
			if (!line.empty()) lines++;
			fclose(in);
		}
		// replaced and stale entries (a file deleted from the card) still sit in the file: rewrite it while it
		// can't grow, so the stale ones go and the file stays at one line an entry
		if (lines > 2 * entries.size() || lines > entries.size() + 10000)
		{
			std::string tmp = file + ".tmp";
			if (FILE* w = fopen(tmp.c_str(), "w"))
			{
				bool ok = true;
				for (auto& e : entries)
				{
					std::string s = line(e.first, e.second);
					if (fwrite(s.data(), 1, s.size(), w) != s.size())
					{
						ok = false;
						break;
					}
				}
				if (ok && fflush(w) != 0) ok = false;
				if (ok && fsync(fileno(w)) != 0) ok = false;
				if (fclose(w) != 0) ok = false;
				// (only a whole rewrite may replace the old file: a full card leaves it as it was)
				if (!ok || rename(tmp.c_str(), file.c_str()) != 0) unlink(tmp.c_str());
			}
		}
		out = fopen(file.c_str(), "a");
		return out != nullptr;
	}

	bool MediaTagCache::find(const std::string& path, long long size, int64_t mtime, MediaTags& out) const
	{
		auto it = entries.find(path);
		if (it == entries.end()) return false;
		const Entry& e = it->second;
		if (e.size != size || e.mtime != mtime) return false;
		out = MediaTags{};
		out.title = words[e.title];
		out.artist = words[e.artist];
		out.albumArtist = words[e.albumArtist];
		out.album = words[e.album];
		out.track = e.track;
		out.disc = e.disc;
		out.year = e.year;
		out.duration = e.duration;
		return true;
	}

	void MediaTagCache::put(const std::string& path, long long size, int64_t mtime, const MediaTags& tags, bool video)
	{
		// (a path with a tab or a newline could never be read back)
		if (path.find_first_of("\t\n") != std::string::npos) return;
		Entry e;
		e.size = size;
		e.mtime = mtime;
		e.video = video;
		e.track = tags.track;
		e.disc = tags.disc;
		e.year = tags.year;
		e.duration = tags.duration > 0 && std::isfinite(tags.duration) ? (int)(tags.duration + 0.5) : -1;
		e.title = word(clean(tags.title));
		e.artist = word(clean(tags.artist));
		e.albumArtist = word(clean(tags.albumArtist));
		e.album = word(clean(tags.album));
		// the same answer again (a pass that walks a folder twice is not one): nothing newer for the file
		auto it = entries.find(path);
		if (it != entries.end() && it->second.size == e.size && it->second.mtime == e.mtime && it->second.video == e.video &&
			it->second.track == e.track && it->second.disc == e.disc && it->second.year == e.year &&
			it->second.duration == e.duration && it->second.title == e.title && it->second.artist == e.artist &&
			it->second.albumArtist == e.albumArtist && it->second.album == e.album)
			return;
		entries[path] = e;
		if (!out) return;
		std::string s = line(path, e);
		fwrite(s.data(), 1, s.size(), out);
		// whole lines before this one stay (power lost on the card loses the one being written, not the run's)
		fflush(out);
	}
}
