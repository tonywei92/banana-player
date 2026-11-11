// The manga reader's worker (docs/design/manga.md 7): one thread runs the source scripts' calls and the built-in
// sources' requests. Requests carry a serial; answers come to take() on the main thread, and each one pushes the SDL
// user event named in start() (at most one waiting in the queue), so the Ui redraws without polling and drops answers
// whose serial is stale. Neither this thread nor this class ever touches Player or Ui.
//
// One lua_State per source (5.4), created on first use and closed after 5 minutes without a call; one call at a time
// per source, which one thread gives for free. Turning the reader off cancels everything and leaves the thread asleep.
// As the Updater and MangaFetch, stop() doesn't wait: a slow server must never hold up a quit.
#pragma once
#include "manganetfolders.h"
#include "mangasource.h"
#include "net.h"
#include <SDL.h>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bp
{
	class MangaWorker
	{
	public:
		// One request. sourceId is a source's full id "<repo-hash>/<id>"; scriptPath is its .lua file (empty: the
		// installed path under the state dir). json is the JSON the host takes for that call: the series, the chapter
		// or the page item, or (Search) the filter values. page and query are Popular/Latest/Search's.
		struct Job
		{
			enum Kind { Popular, Latest, Search, Filters, Details, Chapters, Pages, Image, NetList, NetScan, NetFetch };
			Kind kind = Popular;
			std::string sourceId, scriptPath;
			int page = 1;
			std::string query, json;
			// a newer job for the same source and kind replaces the waiting one (a fast scroll's pages); false: this
			// one waits its turn and isn't replaced, so two pages asking for a source's chapters at once both get them
			bool replace = true;
			// The network folder jobs (design 5.1): NetList lists netPath, NetScan lists it and reads its child
			// folders to lay it out as a series, NetFetch copies a chapter to netDest (a local folder for a folder
			// chapter, a file for an archive). The server and its opened password are the caller's (the Ui); neither
			// is ever kept in a path or in a store.
			NetServer netServer;
			std::string netPassword, netPath, netDest;
			bool netArchive = false;
		};

		// The host's own structs, so nothing of a Lua state outlives its call (5.4).
		struct Result
		{
			uint64_t serial = 0;
			Job::Kind kind = Job::Popular;
			std::string sourceId;
			bool ok = false;
			std::string error;	// the source's own one-line message
			MangaList list;
			MangaDetails details;
			std::vector<MangaChapter> chapters;
			std::vector<MangaPage> pages;
			std::string filtersJson;
			std::string imageUrl;
			std::vector<std::pair<std::string, std::string>> imageHeaders;
			// network folder jobs: a listing, the folder laid out as a series, and the local copy fetched
			std::vector<NetEntry> netEntries;
			NetMangaSeries netSeries;
			std::string netFile;
			// the source's own headers (its Referer and the like) for image fetches, filled for Popular, Latest,
			// Search and Details results so the Ui can fetch cover images with them
			std::vector<std::pair<std::string, std::string>> sourceHeaders;
		};

		MangaWorker();
		~MangaWorker();
		MangaWorker(const MangaWorker&) = delete;
		MangaWorker& operator=(const MangaWorker&) = delete;

		// Starts the thread, which sleeps until a job comes. stateDir is the player's --state-dir: prefs live in
		// <stateDir>/manga/prefs/<repo-hash>-<id>.json (5.4). wakeEvent 0 means the worker pushes no events.
		void start(Uint32 wakeEvent, const std::string& stateDir);
		// Secure DNS (Manga settings): a script's host lookups ask Cloudflare over HTTPS instead of getaddrinfo. The
		// same public-address checks and pinning apply to what it answers. Safe from the main thread while running.
		void setSecureDns(bool on);
		// Queues a job and returns its serial (0 when the worker has stopped). A job still waiting for the same
		// source and kind is replaced by this one, so a fast scroll doesn't queue ten Popular pages.
		uint64_t submit(const Job& job);
		// Forgets a waiting job, aborts a running one (its in-flight request fails, its answer is dropped), or drops
		// an answer not taken yet.
		void cancel(uint64_t serial);
		// Everything: the queue, the running call and the answers not taken. The thread stays asleep for the next
		// use; the reader turning off calls this.
		void cancelAll();
		// Main thread: the answers finished since the last take, in the order the jobs finished.
		std::vector<Result> take();
		// Quit: the thread leaves after the call it is in without waiting for a server (a detached thread).
		void stop();

		// The validator MangaRepos takes (5.3): the script is loaded in a fresh sandbox with a short budget (1 s and a
		// small instruction count) and its contract checked (api, id, the required functions). It may run on the main
		// thread: a bad script costs a second at most, which is accepted at install time.
		static bool validateScript(const std::string& text, const std::string& expectedId, std::string& error);

		// How long a source's state may sit unused before it is closed (5.4: 5 minutes). The test uses a fraction of a
		// second to see the close happen.
		void setIdleSeconds(double seconds);
		// How many sources have a live state right now (the idle-close test's own window on the thread).
		int openSourceCount() const;

	private:
		struct Impl;
		std::shared_ptr<Impl> impl;
	};
}
