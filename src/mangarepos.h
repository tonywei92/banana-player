// Manga source repositories (docs/design/manga.md 5.3): the repositories the user added, their indexes, and the
// sources installed from them. Driven by main's loop, like the rest of the reader: nothing here waits on a server.
// It issues a fetch and handles the answer in onFetched(); the answers are taken from MangaFetch on the main thread.
//
// The Lua host is a separate library (design 5.4), so the "does this script really define this source" check is
// injected: a validator is handed the script text, the id the index claims, and returns why it was refused.
// The fetcher is a tiny interface too, with MangaFetchApi as the player's adapter, so a test can serve its own bytes.
#pragma once
#include "mangafetch.h"	// MangaFetch::Result, which onFetched takes
#include "mangarepo.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bp
{
	// What the manager needs from a fetcher: a GET and a way to forget it. MangaFetch's own get() fits, and the test
	// fake implements these two without a thread or a socket.
	class FetchApi
	{
	public:
		using Headers = std::vector<std::pair<std::string, std::string>>;
		virtual ~FetchApi() = default;
		// 0: it has stopped and will never answer
		virtual uint64_t get(const std::string& url, const Headers& headers = {},
			int64_t maxBytes = 64 * 1024 * 1024) = 0;
		virtual void cancel(uint64_t id) = 0;
	};

	// The player's fetcher. Its methods live in mangarepos.cpp under BP_MANGA_FETCH (the player defines it): the test
	// target compiles the manager without a MangaFetch to link to.
	class MangaFetchApi : public FetchApi
	{
	public:
		explicit MangaFetchApi(MangaFetch& fetch) : fetch_(fetch) {}
		uint64_t get(const std::string& url, const Headers& headers = {},
			int64_t maxBytes = 64 * 1024 * 1024) override;
		void cancel(uint64_t id) override;

	private:
		MangaFetch& fetch_;
	};

	class MangaRepos
	{
	public:
		static constexpr int64_t REFRESH_SECONDS = 60 * 60;	// the design's "at most once an hour"
		static constexpr size_t MAX_SCRIPT_BYTES = 256 * 1024;
		static constexpr size_t MAX_INDEX_BYTES = 1024 * 1024;

		// script text, the id the index says it is, a reason when it is refused. A fresh sandbox's answer (design
		// 5.3): it must return a table whose api is this build's, whose id matches, and with the required functions.
		using Validator = std::function<bool(const std::string& scriptText, const std::string& expectedId,
			std::string& error)>;

		struct Event
		{
			enum Kind { RepoLoaded, RepoFailed, Installed, InstallFailed, Updated } kind = RepoLoaded;
			std::string repo;	// the repository's URL
			std::string source;	// the source's full id "<repo-hash>/<id>", "" for a repository's own event
			std::string message;	// a failure's reason; on success the version, or the repository's name
		};

		// What Sources lists before the downloads happen: one line per repository.
		struct RepoInfo
		{
			std::string url, name, etag;
			int64_t lastFetched = 0;
			bool haveIndex = false;
			size_t sourceCount = 0;
		};

		// A source an added repository lists and this state doesn't have installed.
		struct AvailableSource
		{
			std::string repo, repoName, repoHash, id, fullId;
			std::string name, version, lang, icon;
			bool adult = false;
		};

		// One source in manga/sources/installed.tsv, with design 5.3's never-automatic Update offer.
		struct InstalledSource
		{
			std::string repo, repoHash, id, fullId;
			std::string version, name, lang, file;
			bool adult = false;
			bool updateAvailable = false;
		};

		MangaRepos(FetchApi& fetch, Validator validator = {});
		~MangaRepos();
		MangaRepos(const MangaRepos&) = delete;
		MangaRepos& operator=(const MangaRepos&) = delete;

		// The state folder; the files are in <dir>/manga (design 6). A missing file is an empty manager, a damaged
		// line is skipped, so a bad file never keeps the reader from starting. False only when dir is "".
		bool load(const std::string& stateDir);
		// repos.tsv, installed.tsv and each repository's last good index; each written beside and renamed over. The
		// caller saves after taking its events; a crash must not leave a half file.
		bool save();

		// Starts a fetch of the index at once. False and `error` says why: not a URL, or already added.
		bool addRepo(const std::string& url, std::string& error);
		// Forgets the repository and its cached index. With alsoSources its scripts and prefs go too; without, the
		// installed sources stay (library entries stay either way, design 5.3).
		bool removeRepo(const std::string& url, bool alsoSources);

		// Sources opens: each repository fetched at most once an hour, unless force (Refresh). If-None-Match carries
		// the etag; a 304 keeps the cached index.
		void refresh(bool force);

		std::vector<AvailableSource> available() const;
		std::vector<InstalledSource> installed() const;
		std::vector<RepoInfo> repos() const;

		// Downloads the script, checks its size, SHA-256 and the validator, then writes it. False (and a failure
		// event) when it can't even start; the download's own failure arrives as an InstallFailed event.
		bool install(const std::string& repoUrl, const std::string& sourceId);
		// The same, only when the index lists a higher version than the installed one. Prefs are kept.
		bool update(const std::string& repoUrl, const std::string& sourceId);
		// Deletes the script and stateDir/manga/prefs/<repo-hash>-<id>.json; the record goes with them.
		bool uninstall(const std::string& fullId);

		// True when the result answers one of this manager's requests, which is then handled (and the event queued).
		bool onFetched(const MangaFetch::Result& result);
		std::vector<Event> takeEvents();

		uint64_t serial() const { return serial_; }	// grows with every change: the Sources page redraws on it

		// <repo-hash>: the hex of the URL's SHA-256, first 12 characters. A source's full id is <repo-hash>/<id>.
		static std::string repoHash(const std::string& url);
		static std::string fullId(const std::string& repoHash, const std::string& id);

		// Tests (and a UI that wants a frozen clock) can replace time(nullptr): seconds since the epoch.
		void setClock(std::function<int64_t()> clock);

	private:
		struct Repo
		{
			std::string url, name, etag;
			int64_t lastFetched = 0;
			std::string indexJson;	// the last good index, byte for byte: Sources works offline
			RepoIndex index;
			bool haveIndex = false;
		};
		struct Installed
		{
			std::string repoHash, id, version, name, lang, file;
			bool adult = false;
		};
		enum class PendingKind { Index, Script };
		struct Pending
		{
			PendingKind kind = PendingKind::Index;
			std::string repo;	// the repository URL
			std::string sourceId;
			bool update = false;
		};

		int64_t now() const;
		std::string stateDir_;
		std::string mangaDir() const { return stateDir_ + "/manga"; }
		std::string sourcesDir() const { return mangaDir() + "/sources"; }
		std::string prefsDir() const { return mangaDir() + "/prefs"; }

		Repo* findRepo(const std::string& url);
		const Repo* findRepo(const std::string& url) const;
		Installed* findInstalled(const std::string& fullId);
		const Installed* findInstalled(const std::string& fullId) const;
		bool hasIndexFetch(const std::string& url) const;
		bool startIndexFetch(const Repo& repo);
		bool startScriptFetch(const std::string& repoUrl, const std::string& sourceId, bool update);
		void handleIndex(const Pending& p, const MangaFetch::Result& result);
		void handleScript(const Pending& p, const MangaFetch::Result& result);
		void addEvent(Event::Kind kind, const std::string& repo, const std::string& source,
			const std::string& message);
		void cancelForRepo(const std::string& repoUrl);
		void removeInstalled(const Installed& entry);

		std::string stateDirForIndex(const std::string& hash) const
		{
			return mangaDir() + "/repos/" + hash + ".json";
		}

		FetchApi& fetch_;
		Validator validator_;
		std::function<int64_t()> clock_;
		std::vector<Repo> repos_;
		std::vector<Installed> installed_;
		std::map<uint64_t, Pending> pending_;
		std::vector<Event> events_;
		uint64_t serial_ = 0;
	};
}
