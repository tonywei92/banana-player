// The Manga reader's images and covers, fetched on a thread of their own (docs/design/manga.md section 7): one curl
// multi with 4 transfers at once and 2 per host, so a grid or a chapter loads without stalling the panels. A request
// carries a serial (the id get() returns) and its answer comes to take() on the main thread; the Ui drops answers whose
// serial is stale. Like the Updater, the fetcher is never destroyed: stop() tells its detached thread to leave without
// waiting for a slow server. It never touches Player or Ui; each finished request pushes the SDL user event named in
// start(), at most one event waiting in the queue at a time.
#pragma once
#include <SDL.h>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bp
{
	class MangaFetch
	{
	public:
		// the request's own headers as "Name: value" pairs (a source's Referer, an API's Accept...)
		using Headers = std::vector<std::pair<std::string, std::string>>;
		struct Result
		{
			uint64_t id = 0;
			int status = 0;				// the last HTTP answer (0 when there was none)
			std::string error;			// why it failed; empty when the answer was whole
			std::string contentType;
			Headers headers;			// the answer's own headers, as they came (redirect hops included)
			std::string bodyStart;		// its first bytes: enough for a browser check's own markers
			std::string finalUrl;		// after redirects
			std::vector<uint8_t> body;	// empty when toFile was given, or when the request failed
			std::string file;			// toFile, once renamed in place; empty otherwise
		};

		MangaFetch();
		~MangaFetch();
		MangaFetch(const MangaFetch&) = delete;
		MangaFetch& operator=(const MangaFetch&) = delete;

		// Starts the thread and names the event it pushes (coalesced). Call once, before get(); the thread waits
		// asleep until a request comes.
		void start(Uint32 wakeEvent);
		// Queues a GET and returns its id (0: the fetcher has stopped). headers go as given; maxBytes <= 0 means no
		// limit. With toFile the body is written to toFile + ".part" and renamed in place when whole; without it the
		// body comes back in Result.
		uint64_t get(const std::string& url, const Headers& headers = {}, int64_t maxBytes = 64 * 1024 * 1024,
			const std::string& toFile = "");
		// Forgets a request: a queued one goes, a running one is aborted, a finished answer not yet taken is dropped
		void cancel(uint64_t id);
		// The same for everything (leaving the reader, a source changed)
		void cancelAll();
		// Main thread: the answers finished since the last take
		std::vector<Result> take();
		// At quit: the thread leaves without waiting for the servers (a detached thread, as the other workers)
		void stop();

	private:
		struct Impl;
		std::shared_ptr<Impl> impl;
	};
}
