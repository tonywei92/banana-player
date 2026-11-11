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
			for (auto& p : inFlight) net->fetch().cancel(p.second);
		inFlight.clear();
		byId.clear();
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
		reorder();
		fill();
	}

	bool ReaderFetch::own(MangaFetch::Result& r)
	{
		auto it = byId.find(r.id);
		if (it == byId.end()) return false;
		int index = it->second;
		byId.erase(it);
		inFlight.erase(index);
		bool here = r.error.empty() && !r.file.empty() && index >= 0 && index < (int)urls.size() &&
			cache->has(urls[index]);
		if (here)
		{
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
		for (int index : ReaderFetchPlan::stale(st))
		{
			auto it = inFlight.find(index);
			if (it == inFlight.end()) continue;
			net->fetch().cancel(it->second);
			byId.erase(it->second);
			inFlight.erase(it);
			reader->pageDropped(index);
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
		}
	}
}
