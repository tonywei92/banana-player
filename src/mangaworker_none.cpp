// No BP_NET build: libcurl isn't linked, so no source script can run. Every job is answered at once with one line,
// and the reader shows it, rather than waiting for an event that can never come. Same shape as the real worker
// (src/mangaworker.cpp), minus the thread and the Lua host.
#include "mangaworker.h"
#include <mutex>

namespace bp
{
	namespace
	{
		const char* const kNoNetwork = "No network in this build";
	}

	struct MangaWorker::Impl
	{
		std::mutex m;
		std::vector<Result> results;
		std::string stateDir;
		Uint32 wakeEvent = 0;
		uint64_t nextSerial = 1;
		double idleSeconds = 5 * 60;
		bool started = false, stopping = false, eventPending = false;
	};

	MangaWorker::MangaWorker() : impl(new Impl) {}
	MangaWorker::~MangaWorker() = default;

	void MangaWorker::start(Uint32 wakeEvent, const std::string& stateDir)
	{
		std::lock_guard<std::mutex> g(impl->m);
		if (impl->started || impl->stopping) return;
		impl->started = true;
		impl->wakeEvent = wakeEvent;
		impl->stateDir = stateDir;
	}

	uint64_t MangaWorker::submit(const Job& job)
	{
		Uint32 wake = 0;
		uint64_t serial = 0;
		{
			std::lock_guard<std::mutex> g(impl->m);
			if (impl->stopping) return 0;
			serial = impl->nextSerial++;
			Result r;
			r.serial = serial;
			r.kind = job.kind;
			r.sourceId = job.sourceId;
			r.error = kNoNetwork;
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
		return serial;
	}

	void MangaWorker::cancel(uint64_t serial)
	{
		std::lock_guard<std::mutex> g(impl->m);
		for (auto it = impl->results.begin(); it != impl->results.end(); ++it)
		{
			if (it->serial == serial)
			{
				impl->results.erase(it);
				return;
			}
		}
	}

	void MangaWorker::cancelAll()
	{
		std::lock_guard<std::mutex> g(impl->m);
		impl->results.clear();
	}

	std::vector<MangaWorker::Result> MangaWorker::take()
	{
		std::lock_guard<std::mutex> g(impl->m);
		impl->eventPending = false;
		std::vector<Result> out;
		out.swap(impl->results);
		return out;
	}

	void MangaWorker::stop()
	{
		std::lock_guard<std::mutex> g(impl->m);
		impl->stopping = true;
		impl->results.clear();
	}

	bool MangaWorker::validateScript(const std::string&, const std::string&, std::string& error)
	{
		error = kNoNetwork;
		return false;
	}

	void MangaWorker::setIdleSeconds(double seconds)
	{
		std::lock_guard<std::mutex> g(impl->m);
		impl->idleSeconds = seconds > 0 ? seconds : 0;
	}

	int MangaWorker::openSourceCount() const
	{
		return 0;
	}
}
