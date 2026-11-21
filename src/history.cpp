#include "history.h"
#include "browser.h"
#include "netpath.h"
#include "pagebook.h"
#include "playlist.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <sstream>

namespace bp
{
	namespace
	{
		// a civil day's number, from its date (Howard Hinnant's days_from_civil): days never depend on the clock's
		// DST shifts, so yesterday is yesterday after a change of hour too
		long long dayNumber(int y, int m, int d)
		{
			y -= m <= 2;
			const long long era = (y >= 0 ? y : y - 399) / 400;
			const long long yoe = y - era * 400;
			const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
			const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
			return era * 146097 + doe - 719468;
		}
		long long dayOf(time_t t)
		{
			struct tm tmv;
			if (!localtime_r(&t, &tmv)) return 0;
			return dayNumber(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
		}
	}

	const char* PlayHistory::dayName(int g)
	{
		switch (g)
		{
		case TODAY: return "Today";
		case YESTERDAY: return "Yesterday";
		default: return "Earlier";
		}
	}

	int PlayHistory::dayGroup(time_t at, time_t now)
	{
		if (at == NO_TIME) return EARLIER;
		long long d = dayOf(at), n = dayOf(now);
		if (d >= n) return TODAY;
		if (d == n - 1) return YESTERDAY;
		return EARLIER;
	}

	namespace
	{
		const char* const KIND_WORDS[] = {"", "playlist", "album", "artist", "music-folder", "random", "folder"};
		constexpr int KINDS = (int)(sizeof(KIND_WORDS) / sizeof(KIND_WORDS[0]));

		std::vector<std::string> splitTabs(const std::string& line)
		{
			std::vector<std::string> out;
			size_t a = 0;
			while (true)
			{
				size_t b = line.find('\t', a);
				out.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a));
				if (b == std::string::npos) return out;
				a = b + 1;
			}
		}

		// a list's id in the file: FNV-1a over its paths (and where Keep playing's songs begin) (the same list, played from by several songs, written once)
		std::string listId(const std::vector<std::string>& list, int addedAt)
		{
			uint64_t h = 1469598103934665603ull;
			// (the same songs marked otherwise are another list)
			if (addedAt >= 0) { h ^= (uint64_t)addedAt + 1; h *= 1099511628211ull; }
			for (auto& p : list)
			{
				for (unsigned char c : p) { h ^= c; h *= 1099511628211ull; }
				h ^= 0xff;
				h *= 1099511628211ull;
			}
			char buf[20];
			snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
			return buf;
		}

		bool isPrefix(const std::vector<std::string>& a, const std::vector<std::string>& b)
		{
			return a.size() < b.size() && std::equal(a.begin(), a.end(), b.begin());
		}

		std::string lastPart(const std::string& path)
		{
			size_t s = path.find_last_of('/');
			return s == std::string::npos || s + 1 == path.size() ? path : path.substr(s + 1);
		}
	}

	std::string RecentContext::title() const
	{
		switch (kind)
		{
		case ALBUM: return "Album \xC2\xB7 " + name;
		case ARTIST: return "Artist \xC2\xB7 " + name;
		case MUSIC_FOLDER: return "Folder \xC2\xB7 " + lastPart(name);
		case RANDOM: return name;
		case PLAYLIST: case FOLDER: return lastPart(name);
		default: return "";
		}
	}

	RecentContext recentContextOf(const std::string& song, const std::vector<std::string>& queue, int index,
		int sourceKind, const std::string& title, const std::string& sourceId, bool rail,
		const std::vector<std::string>& added)
	{
		RecentContext c;
		if (index < 0 || index >= (int)queue.size() || queue[index] != song) return c;
		c.index = index;
		if (sourceKind == 2)
		{
			// (a playlist the pages made of a selection has no name: nothing to go back to)
			if (sourceId.empty()) return RecentContext();
			c.kind = RecentContext::PLAYLIST;
			c.name = sourceId;
			return c;
		}
		if (rail)
		{
			// the Music rail's lists, told apart by the title Music gave them (ui_music.cpp's play)
			static const std::string ALBUM = "Album \xC2\xB7 ", ARTIST = "Artist \xC2\xB7 ", FOLDER = "Folder \xC2\xB7 ";
			auto starts = [&](const std::string& w) { return title.compare(0, w.size(), w) == 0; };
			if (starts(ALBUM)) { c.kind = RecentContext::ALBUM; c.name = title.substr(ALBUM.size()); }
			else if (starts(ARTIST)) { c.kind = RecentContext::ARTIST; c.name = title.substr(ARTIST.size()); }
			else if (starts(FOLDER)) { c.kind = RecentContext::MUSIC_FOLDER; c.name = parentFolder(song); }
			else { c.kind = RecentContext::RANDOM; c.name = title.empty() ? "Random" : title; }
			// (a list Keep playing grew long: the songs around this one)
			size_t from = 0, to = queue.size();
			if (queue.size() > RecentContext::LIST_MAX)
			{
				from = (size_t)std::max(0, index - (int)RecentContext::LIST_MAX / 2);
				to = std::min(queue.size(), from + RecentContext::LIST_MAX);
				from = to - RecentContext::LIST_MAX;
			}
			c.list.assign(queue.begin() + (long)from, queue.begin() + (long)to);
			c.index = index - (int)from;
			// (Keep playing's songs go on after the list's own: the first of them marks where its heading goes)
			for (int k = 0; k < (int)c.list.size() && c.addedAt < 0; k++)
				if (std::find(added.begin(), added.end(), c.list[k]) != added.end()) c.addedAt = k;
			return c;
		}
		// a folder's files (Files, a network folder): every one in the song's folder, else a selection (none)
		std::string dir = isNetPath(song) ? netParent(song) : parentFolder(song);
		for (auto& q : queue)
			if ((isNetPath(q) ? netParent(q) : parentFolder(q)) != dir) return RecentContext();
		c.kind = RecentContext::FOLDER;
		c.name = dir;
		return c;
	}

	std::string recentMissing(const std::string& path, bool there, const std::vector<RemovableCard>& cards)
	{
		if (there) return "";
		for (auto& c : cards)
			if (!c.in && path.compare(0, c.path.size() + 1, c.path + "/") == 0) return c.name + " is out";
		return "Not on this handheld any more";
	}

	void PlayHistory::parse(const std::string& text)
	{
		paths.clear();
		ats.clear();
		ctxs.clear();
		std::unordered_map<std::string, std::vector<std::string>> lists;
		std::unordered_map<std::string, int> listAdded;	// each list's first song Keep playing added (-1 none)
		std::vector<std::string> listOf;	// each entry's list id
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.compare(0, 6, "#list\t") == 0)
			{
				auto f = splitTabs(line);
				// (where Keep playing's songs begin, "added=N", before the paths: a path never starts so)
				size_t first = 2;
				int at = -1;
				if (f.size() > 2 && f[2].compare(0, 6, "added=") == 0) { at = atoi(f[2].c_str() + 6); first = 3; }
				if (f.size() >= 2)
				{
					lists[f[1]].assign(f.begin() + (long)std::min(first, f.size()), f.end());
					listAdded[f[1]] = at;
				}
				continue;
			}
			if (line.empty() || line[0] == '#') continue;
			// a path, then (a tab and) when it played, then a song's context; an older file has the path alone
			auto f = splitTabs(line);
			const std::string& path = f[0];
			if (path.empty() || std::find(paths.begin(), paths.end(), path) != paths.end()) continue;
			paths.push_back(path);
			ats.push_back(f.size() < 2 || f[1].empty() ? NO_TIME : (time_t)std::strtoll(f[1].c_str(), nullptr, 10));
			RecentContext c;
			if (f.size() >= 6)
				for (int k = 1; k < KINDS; k++)
					if (f[2] == KIND_WORDS[k])
					{
						c.kind = (RecentContext::Kind)k;
						c.name = f[3];
						c.index = atoi(f[5].c_str());
					}
			ctxs.push_back(c);
			listOf.push_back(f.size() >= 6 ? f[4] : "");
		}
		// (lines in any order: the lists are joined to their songs once all are read)
		for (size_t i = 0; i < ctxs.size(); i++)
		{
			if (!ctxs[i].rail()) continue;
			auto l = lists.find(listOf[i]);
			if (l == lists.end() || ctxs[i].index < 0 || ctxs[i].index >= (int)l->second.size()) ctxs[i] = RecentContext();
			else
			{
				ctxs[i].list = l->second;
				int at = listAdded[listOf[i]];
				ctxs[i].addedAt = at >= 0 && at < (int)l->second.size() ? at : -1;
			}
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
		for (size_t i = 0; i < paths.size();)
			if (n[kindOf(paths[i])]++ >= MAX) eraseAt(i);
			else i++;
	}

	void PlayHistory::eraseAt(size_t i)
	{
		paths.erase(paths.begin() + (long)i);
		if (i < ats.size()) ats.erase(ats.begin() + (long)i);
		if (i < ctxs.size()) ctxs.erase(ctxs.begin() + (long)i);
	}

	std::string PlayHistory::serialize() const
	{
		// the time is the tab and when it played: a file an older version wrote is still read (the path alone)
		// a song's context after it (an older version reads the time's digits and stops); a rail list once, after all
		std::string out = "# banana-player: the videos played, most recent first (path, then when)\n";
		std::vector<std::string> written;
		std::string listLines;
		for (size_t i = 0; i < paths.size(); i++)
		{
			const RecentContext* c = i < ctxs.size() && ctxs[i].kind != RecentContext::NONE ? &ctxs[i] : nullptr;
			bool at = i < ats.size() && ats[i] != NO_TIME;
			out += paths[i];
			if (at || c) out += "\t" + (at ? std::to_string((long long)ats[i]) : std::string());
			if (c)
			{
				std::string id = c->rail() ? listId(c->list, c->addedAt) : "";
				out += std::string("\t") + KIND_WORDS[c->kind] + "\t" + c->name + "\t" + id + "\t" + std::to_string(c->index);
				if (!id.empty() && std::find(written.begin(), written.end(), id) == written.end())
				{
					written.push_back(id);
					listLines += "#list\t" + id;
					if (c->addedAt >= 0) listLines += "\tadded=" + std::to_string(c->addedAt);
					for (auto& p : c->list) listLines += "\t" + p;
					listLines += "\n";
				}
			}
			out += "\n";
		}
		return out + listLines;
	}

	void PlayHistory::add(const std::string& path, time_t at)
	{
		for (size_t i = 0; i < paths.size(); i++)
			if (paths[i] == path)
			{
				eraseAt(i);
				break;
			}
		paths.insert(paths.begin(), path);
		ats.insert(ats.begin(), at);
		ctxs.insert(ctxs.begin(), RecentContext());
		trim();
	}

	bool PlayHistory::setContext(const std::string& path, RecentContext ctx)
	{
		auto it = std::find(paths.begin(), paths.end(), path);
		if (it == paths.end()) return false;
		size_t i = (size_t)(it - paths.begin());
		if (ctxs[i] == ctx) return false;
		// a list grown from one others played from (Keep playing): theirs is this one now, their places unchanged
		if (ctx.rail())
			for (auto& c : ctxs)
				if (c.rail() && isPrefix(c.list, ctx.list))
				{
					c.list = ctx.list;
					c.addedAt = ctx.addedAt;
				}
		ctxs[i] = std::move(ctx);
		return true;
	}

	const RecentContext& PlayHistory::contextOf(const std::string& path) const
	{
		static const RecentContext none;
		for (size_t i = 0; i < paths.size(); i++)
			if (paths[i] == path) return i < ctxs.size() ? ctxs[i] : none;
		return none;
	}

	bool PlayHistory::remove(const std::string& path)
	{
		bool any = false;
		for (size_t i = 0; i < paths.size();)
			if (paths[i] == path)
			{
				eraseAt(i);
				any = true;
			}
			else i++;
		return any;
	}

	void PlayHistory::removeIf(bool (*drop)(const std::string&))
	{
		for (size_t i = 0; i < paths.size();)
			if (drop(paths[i])) eraseAt(i);
			else i++;
	}

	bool PlayHistory::moved(const std::string& from, const std::string& to)
	{
		bool any = false;
		std::string n;
		for (auto& p : paths)
			if (movedPath(p, from, to, n)) { p = n; any = true; }
		// the lists and folders the songs played from follow too (a playlist is named, not a path)
		for (auto& c : ctxs)
		{
			if ((c.kind == RecentContext::FOLDER || c.kind == RecentContext::MUSIC_FOLDER) && movedPath(c.name, from, to, n)) { c.name = n; any = true; }
			for (auto& p : c.list)
				if (movedPath(p, from, to, n)) { p = n; any = true; }
		}
		return any;
	}

	time_t PlayHistory::timeOf(const std::string& path) const
	{
		for (size_t i = 0; i < paths.size(); i++)
			if (paths[i] == path) return i < ats.size() ? ats[i] : NO_TIME;
		return NO_TIME;
	}
}
