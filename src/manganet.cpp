#include "manganet.h"
#include "mangajava.h"
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
		// the Java add-on's own state and wake event (its download and process threads never touch the Ui)
		javaHost.configure(workerEvent, stateDir);
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
		javaState.setSubmit([this](const std::string& body) { return javaHost.graphql(body); });
		javaState.load(state);
		dlq.load(state);
		dl = std::make_unique<MangaDownloader>(*api, dlq);
		dl->setFolders(dlRoot, state + "/manga/downloading");
		serverStore.load(state);
		fprintf(stderr, "manga: network started (downloads in %s)\n", dlRoot.c_str());
	}

	void MangaNet::off()
	{
		// the Java runtime is the reader's too, but it can be installed and starting with the rest still asleep
		javaHost.stop();
		javaState.cancelAll();
		if (!running) return;
		dl->pauseAll();
		dlq.save();
		fetcher.cancelAll();
		work.cancelAll();
		fetched.clear();
		workerDone.clear();
		javaDone.clear();
		own++;
	}

	void MangaNet::stop()
	{
		javaHost.stop();
		javaState.cancelAll();
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
			// the reader's pages first: an answer it owns is not one the Ui should ever see
			if (fetchOwner && fetchOwner(r)) continue;
			fetched.push_back(std::move(r));
			own++;
		}
	}

	void MangaNet::onWorkerEvent()
	{
		if (!running) return;
		for (auto& r : work.take()) workerDone.push_back(std::move(r));
		// the Java host shares this wake event: the manager takes its own requests, the rest go to the Ui
		for (auto& r : javaHost.takeGraphQl())
		{
			if (javaState.onAnswer(r.serial, r.body, r.error)) continue;
			javaDone.push_back(std::move(r));
		}
		own++;
	}

	void MangaNet::pump(int64_t nowMs)
	{
		// the Java process' idle stop is watched even when the rest of the reader's network side never started
		javaHost.pump(nowMs);
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

	std::vector<JavaExtHost::GraphQlResult> MangaNet::takeJava()
	{
		std::vector<JavaExtHost::GraphQlResult> out;
		out.swap(javaDone);
		return out;
	}

	uint64_t MangaNet::serial() const
	{
		if (!running) return 0;
		return own + dlq.serial() + dl->serial() + repoMgr->serial() + javaHost.serial() + javaState.serial();
	}
}
