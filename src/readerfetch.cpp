#include "readerfetch.h"
#include "challenge.h"
#include "manganet.h"
#include "readerfetchplan.h"
#include <cerrno>
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		// The fetcher writes straight to the cache's path, which sits in a two-hex folder PageCache makes only when
		// it writes a page itself: the folder is made here before the transfer starts.
		bool makeParent(const std::string& path)
		{
			size_t slash = path.find_last_of('/');
			if (slash == std::string::npos) return true;
			std::string dir = path.substr(0, slash), cur;
			size_t i = 0;
			if (!dir.empty() && dir[0] == '/')
			{
				cur = "/";
				i = 1;
			}
			while (i <= dir.size())
			{
				size_t j = dir.find('/', i);
				if (j == std::string::npos) j = dir.size();
				std::string part = dir.substr(i, j - i);
				if (!part.empty())
				{
					if (cur.empty() || cur.back() == '/') cur += part;
					else cur += "/" + part;
					if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
				}
				if (j == dir.size()) break;
				i = j + 1;
			}
			return true;
		}

		uint64_t fileSize(const std::string& path)
		{
			struct stat st;
			return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? (uint64_t)st.st_size : 0;
		}
	}

	void ReaderFetch::begin(MangaNet* n, PageCache* pc, MangaReader* r, const std::vector<std::string>& pageUrls,
		const MangaReader::Headers& pageHeaders, uint64_t cacheLimit)
	{
		end();
		net = n;
		cache = pc;
		reader = r;
		urls = pageUrls;
		headers = pageHeaders;
		limit = cacheLimit;
	}

	void ReaderFetch::end()
	{
		if (net)
			while (!inFlight.empty())
			{
				auto it = inFlight.begin();
				cancelOne(it->first, it->second, "reader closed");
			}
		inFlight.clear();
		byId.clear();
		started.clear();
		urls.clear();
		headers.clear();
		limit = 0;
		net = nullptr;
		cache = nullptr;
		reader = nullptr;
	}

	void ReaderFetch::pass()
	{
		if (!net || !cache || !reader || !reader->isOpen() || !reader->remote()) return;
		// every pass, not only when a slot frees: the view may have moved away from what is out (and a cancelled
		// answer that still arrives is then thrown away by own())
		reorder();
		fill();
	}

	void ReaderFetch::cancelOne(int index, uint64_t id, const char* why)
	{
		if (net) net->fetch().cancel(id);
		auto st = started.find(id);
		uint64_t ms = st == started.end() ? 0 : (uint64_t)(SDL_GetTicks() - (Uint32)st->second);
		started.erase(id);
		uint64_t bytes = 0;
		if (cache && index >= 0 && index < (int)urls.size())
			bytes = fileSize(cache->pathFor(urls[index]) + ".part");
		byId.erase(id);
		inFlight.erase(index);
		// remembered so an answer still on its way is claimed and thrown away; a bound keeps a very long reading
		// session's cancels from growing a set nothing else ever clears
		if (dropped.size() >= 256) dropped.erase(dropped.begin());
		dropped[id] = index;
		// the reader's own in-flight mark goes too, or pagesToFetch would think the page is still out
		if (reader) reader->pageDropped(index);
		fprintf(stderr, "reader: page %d fetch cancelled after %llu ms, %llu bytes (%s)\n", index + 1,
			(unsigned long long)ms, (unsigned long long)bytes, why);
	}

	bool ReaderFetch::own(MangaFetch::Result& r)
	{
		// a request cancelled while this answer was on its way: claim it so it is never decoded or shown
		auto droppedIt = dropped.find(r.id);
		if (droppedIt != dropped.end())
		{
			int index = droppedIt->second;
			dropped.erase(droppedIt);
			fprintf(stderr, "reader: page %d fetch answer after cancel, dropped\n", index + 1);
			return true;
		}
		auto it = byId.find(r.id);
		if (it == byId.end()) return false;
		int index = it->second;
		byId.erase(it);
		inFlight.erase(index);
		auto st = started.find(r.id);
		uint64_t ms = st == started.end() ? 0 : (uint64_t)(SDL_GetTicks() - (Uint32)st->second);
		started.erase(r.id);
		bool here = r.error.empty() && !r.file.empty() && index >= 0 && index < (int)urls.size() &&
			cache->has(urls[index]);
		uint64_t bytes = 0;
		if (index >= 0 && index < (int)urls.size())
			bytes = cache->has(urls[index]) ? fileSize(cache->pathFor(urls[index])) : r.body.size();
		if (here)
		{
			fprintf(stderr, "reader: page %d fetch done in %llu ms, %llu bytes\n", index + 1,
				(unsigned long long)ms, (unsigned long long)bytes);
			// the fetcher wrote past PageCache, so the cap is applied here (the oldest pages go)
			cache->setLimit(limit);
			reader->pageArrived(index);
		}
		else
		{
			// A challenge page's "HTTP 403" says nothing useful: name the check the source is behind instead.
			std::string error = r.error.empty() ? "the file couldn't be cached" : r.error;
			BrowserCheck check = detectBrowserCheck(r.status, r.headers, r.bodyStart);
			if (check != BrowserCheck::None) error = browserCheckMessage(check);
			fprintf(stderr, "reader: page %d fetch done in %llu ms, %llu bytes (failed: %s)\n", index + 1,
				(unsigned long long)ms, (unsigned long long)bytes, error.c_str());
			reader->pageFailed(index, error);
		}
		return true;
	}

	void ReaderFetch::reorder()
	{
		// the reader moved: a page it no longer wants at all is let go (its bytes would only arrive for nothing)
		ReaderFetchPlan::State st;
		st.wanted = reader->wantedPages();
		for (auto& p : inFlight) st.inFlight.insert(p.first);
		st.target = reader->jumpTarget();
		st.targetAt = reader->jumpAt();
		st.now = (int64_t)SDL_GetTicks();
		st.targetIn = reader->jumpTargetIn();
		std::set<int> wanted(st.wanted.begin(), st.wanted.end());
		for (int index : ReaderFetchPlan::stale(st))
		{
			auto it = inFlight.find(index);
			if (it == inFlight.end()) continue;
			cancelOne(index, it->second, wanted.count(index) ? "jump target first" : "no longer wanted");
		}
	}

	void ReaderFetch::fill()
	{
		int room = MAX_IN_FLIGHT - (int)inFlight.size();
		if (room <= 0) return;
		for (auto& p : reader->pagesToFetch(room))
		{
			int index = p.first;
			std::string path = cache->pathFor(p.second);
			if (!makeParent(path))
			{
				reader->pageFailed(index, "the cache folder couldn't be made");
				continue;
			}
			uint64_t id = net->fetch().get(p.second, headers, 30 * 1024 * 1024, path);
			if (!id)
			{
				reader->pageFailed(index, "the fetcher is stopped");
				continue;
			}
			inFlight[index] = id;
			byId[id] = index;
			started[id] = (uint64_t)SDL_GetTicks();
			fprintf(stderr, "reader: page %d fetch start\n", index + 1);
		}
	}
}
