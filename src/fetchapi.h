#pragma once
// What the manga reader's engines (the repository manager, the downloader) ask of a fetcher: a GET and a way to
// forget it. MangaFetch fits it through MangaFetchApi; tests hand the engines a fake, with no thread and no server.
#include "mangafetch.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bp
{
	class FetchApi
	{
	public:
		using Headers = std::vector<std::pair<std::string, std::string>>;
		virtual ~FetchApi() = default;
		// the request's id (0: the fetcher has stopped and will never answer); toFile: the body goes there (renamed in
		// place when whole) instead of memory
		virtual uint64_t get(const std::string& url, const Headers& headers = {}, int64_t maxBytes = 64 * 1024 * 1024,
			const std::string& toFile = "") = 0;
		virtual void cancel(uint64_t id) = 0;
	};

#ifdef BP_MANGA_FETCH
	// The player's fetcher (only the player defines BP_MANGA_FETCH: the engines' tests have no MangaFetch to link, and
	// GCC keeps these virtual functions, which call it, even unused)
	class MangaFetchApi final : public FetchApi
	{
	public:
		explicit MangaFetchApi(MangaFetch& fetch) : fetch_(fetch) {}
		uint64_t get(const std::string& url, const Headers& headers = {}, int64_t maxBytes = 64 * 1024 * 1024,
			const std::string& toFile = "") override
		{
			return fetch_.get(url, headers, maxBytes, toFile);
		}
		void cancel(uint64_t id) override { fetch_.cancel(id); }

	private:
		MangaFetch& fetch_;
	};
#endif
}
