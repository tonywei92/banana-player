// No BP_NET build: libcurl isn't linked, so the Manga reader can't fetch. Every request fails at once with one line,
// and the reader shows it, rather than waiting for an answer that can never come. Same shape as the real fetcher
// (src/mangafetch.cpp), minus the thread.
#include "mangafetch.h"
#include <mutex>

namespace bp
{
	struct MangaFetch::Impl
	{
		std::mutex m;
		std::vector<Result> results;
		uint64_t nextId = 1;
		Uint32 wakeEvent = 0;
		bool started = false, stopping = false, eventPending = false;
	};

	MangaFetch::MangaFetch() : impl(new Impl) {}
	MangaFetch::~MangaFetch()
	{
		stop();
	}

	void MangaFetch::start(Uint32 wakeEvent)
	{
		std::lock_guard<std::mutex> g(impl->m);
		if (impl->started || impl->stopping) return;
		impl->started = true;
		impl->wakeEvent = wakeEvent;
	}

	uint64_t MangaFetch::get(const std::string&, const Headers&, int64_t, const std::string&)
	{
		Uint32 wake = 0;
		uint64_t id = 0;
		{
			std::lock_guard<std::mutex> g(impl->m);
			if (impl->stopping) return 0;
			id = impl->nextId++;
			Result r;
			r.id = id;
			r.error = "No network in this build";
			impl->results.push_back(std::move(r));
			if (!impl->eventPending && impl->wakeEvent)
			{
				impl->eventPending = true;
				wake = impl->wakeEvent;
			}
		}
		if (wake)
		{
			SDL_Event ev{};
			ev.type = wake;
			SDL_PushEvent(&ev);
		}
		return id;
	}

	void MangaFetch::cancel(uint64_t id)
	{
		std::lock_guard<std::mutex> g(impl->m);
		for (auto it = impl->results.begin(); it != impl->results.end(); ++it)
		{
			if (it->id == id)
			{
				impl->results.erase(it);
				return;
			}
		}
	}

	void MangaFetch::cancelAll()
	{
		std::lock_guard<std::mutex> g(impl->m);
		impl->results.clear();
	}

	std::vector<MangaFetch::Result> MangaFetch::take()
	{
		std::lock_guard<std::mutex> g(impl->m);
		impl->eventPending = false;
		std::vector<Result> out;
		out.swap(impl->results);
		return out;
	}

	void MangaFetch::stop()
	{
		std::lock_guard<std::mutex> g(impl->m);
		impl->stopping = true;
	}
}
