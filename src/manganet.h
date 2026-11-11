#pragma once
// The manga reader's network side in one place (docs/design/manga.md section 7): the fetcher, the source worker, the
// repositories, the download queue and its engine, the saved servers. Main owns it and drives it from its loop (wake
// events, pump); the bottom panel (Ui) reaches it through a pointer, on the main thread only. Nothing of it starts
// until the reader is first used with the extension on (off: nothing runs, no threads), and it is never destroyed
// (its threads never hold up quitting: stop()).
#include "dlqueue.h"
#include "fetchapi.h"
#include "javaext.h"
#include "mangadownloader.h"
#include "mangafetch.h"
#include "mangajava.h"
#include "mangarepos.h"
#include "mangaservers.h"
#include "mangaworker.h"
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bp
{
	class MangaNet
	{
	public:
		// the events main registered for the fetcher's and the worker's answers; the state folder; the card places
		// (the Files roots) to put the downloads' Manga folder in
		void configure(Uint32 fetchEvent, Uint32 workerEvent, const std::string& stateDir, const std::vector<std::string>& places);
		// threads and stores, the first time (idempotent)
		void start();
		bool started() const { return running; }
		// Secure DNS (Manga settings): the fetcher's requests and the source worker's host lookups go through
		// Cloudflare over HTTPS. Off is the system's lookup; safe to call while the threads run.
		void setSecureDns(bool on)
		{
			fetcher.setSecureDns(on);
			work.setSecureDns(on);
			javaHost.setSecureDns(on);
		}
		// the extension turned off: everything asked of it goes (the threads stay, asleep)
		void off();
		// quitting: the threads leave without waiting for servers
		void stop();

		// main's loop: the fetcher's answers routed (the downloader's, the repositories', the rest kept for the Ui)
		void onFetchEvent();
		// the worker's answers kept for the Ui
		void onWorkerEvent();
		// every pass: the downloads move on
		void pump(int64_t nowMs);

		// for the Ui
		MangaFetch& fetch() { return fetcher; }
		FetchApi& fetchApi() { return *api; }
		MangaWorker& worker() { return work; }
		MangaRepos& repos() { return *repoMgr; }
		DownloadQueue& queue() { return dlq; }
		MangaDownloader& downloader() { return *dl; }
		MangaServerStore& servers() { return serverStore; }
		// the Java extensions' runtime and server: installed from Manga settings, started while a Java source is in use
		JavaExtHost& javaExt() { return javaHost; }
		// the Java repositories, the extensions Suwayomi lists, and the sources an install returned
		MangaJava& javaSources() { return javaState; }
		const MangaJava& javaSources() const { return javaState; }
		const std::string& downloadsRoot() const { return dlRoot; }
		// answers nobody in here claimed (the Ui's own fetches: a server's catalogue, a cover by URL)
		std::vector<MangaFetch::Result> takeFetched();
		// the reader's page fetches take their own answers by id before they reach takeFetched, so the Ui's own
		// fetches are unaffected and the reader's answers are never lost to whichever takes first (null: none)
		void setFetchOwner(std::function<bool(MangaFetch::Result&)> owner) { fetchOwner = std::move(owner); }
		std::vector<MangaWorker::Result> takeWorker();
		// the Java GraphQL answers the manager didn't own (the Ui's own: a source's filters, list, series, pages)
		std::vector<JavaExtHost::GraphQlResult> takeJava();
		// grows when anything the Ui shows changed (a queue step, a repository loaded, an answer waiting)
		uint64_t serial() const;

	private:
		bool running = false;
		Uint32 evFetch = 0, evWorker = 0;
		std::string state, dlRoot;
		MangaFetch fetcher;
		std::unique_ptr<FetchApi> api;
		MangaWorker work;
		std::unique_ptr<MangaRepos> repoMgr;
		DownloadQueue dlq;
		std::unique_ptr<MangaDownloader> dl;
		MangaServerStore serverStore;
		JavaExtHost javaHost;
		MangaJava javaState;
		std::vector<MangaFetch::Result> fetched;
		std::vector<MangaWorker::Result> workerDone;
		std::vector<JavaExtHost::GraphQlResult> javaDone;
		std::function<bool(MangaFetch::Result&)> fetchOwner;
		uint64_t own = 0;
	};
}
