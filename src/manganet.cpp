#include "manganet.h"
#include <cstdio>
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		bool isDirectory(const std::string& p)
		{
			struct stat st;
			return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
		}
	}

	void MangaNet::configure(Uint32 fetchEvent, Uint32 workerEvent, const std::string& stateDir, const std::vector<std::string>& places)
	{
		evFetch = fetchEvent;
		evWorker = workerEvent;
		state = stateDir;
		// the downloads' folder: an existing Manga folder on a card first (what's read from there already), else one
		// on the main card (the handheld's /data, not the videos folder), else the first place
		dlRoot.clear();
		for (auto& p : places)
			for (const char* n : {"/Manga", "/manga", "/MANGA"})
				if (dlRoot.empty() && isDirectory(p + n)) dlRoot = p + n;
		if (dlRoot.empty())
		{
			std::string base = places.empty() ? state : places.front();
			for (auto& p : places)
				if (p == "/data") base = p;
			dlRoot = base + "/Manga";
		}
	}

	void MangaNet::start()
	{
		if (running || state.empty()) return;
		running = true;
		fetcher.start(evFetch);
		work.start(evWorker, state);
#ifdef BP_MANGA_FETCH
		api = std::make_unique<MangaFetchApi>(fetcher);
#endif
		repoMgr = std::make_unique<MangaRepos>(*api, &MangaWorker::validateScript);
		repoMgr->load(state);
		dlq.load(state);
		dl = std::make_unique<MangaDownloader>(*api, dlq);
		dl->setFolders(dlRoot, state + "/manga/downloading");
		serverStore.load(state);
		fprintf(stderr, "manga: network started (downloads in %s)\n", dlRoot.c_str());
	}

	void MangaNet::off()
	{
		if (!running) return;
		dl->pauseAll();
		dlq.save();
		fetcher.cancelAll();
		work.cancelAll();
		fetched.clear();
		workerDone.clear();
		own++;
	}

	void MangaNet::stop()
	{
		if (!running) return;
		dlq.save();
		fetcher.stop();
		work.stop();
	}

	void MangaNet::onFetchEvent()
	{
		if (!running) return;
		for (auto& r : fetcher.take())
		{
			if (dl->onFetched(r) || repoMgr->onFetched(r)) continue;
			fetched.push_back(std::move(r));
			own++;
		}
	}

	void MangaNet::onWorkerEvent()
	{
		if (!running) return;
		for (auto& r : work.take()) workerDone.push_back(std::move(r));
		own++;
	}

	void MangaNet::pump(int64_t nowMs)
	{
		if (running) dl->pump(nowMs);
	}

	std::vector<MangaFetch::Result> MangaNet::takeFetched()
	{
		std::vector<MangaFetch::Result> out;
		out.swap(fetched);
		return out;
	}

	std::vector<MangaWorker::Result> MangaNet::takeWorker()
	{
		std::vector<MangaWorker::Result> out;
		out.swap(workerDone);
		return out;
	}

	uint64_t MangaNet::serial() const
	{
		if (!running) return 0;
		return own + dlq.serial() + dl->serial() + repoMgr->serial();
	}
}
