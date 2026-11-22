// The Java extensions the reader uses (docs/design/manga.md section 2): the Tachiyomi / Mihon repositories the
// user added, the extensions Suwayomi lists for them, and the sources an install returns. Window-free and
// network-free like mangarepos: it builds the GraphQL bodies with suwayomi.*, hands them to a Submit (the host's
// channel), and reads the answers the caller gives back. Serials it doesn't know are left for the UI's own requests.
//
// A Java source's full id is "java:<Suwayomi source id>". Its series live under "src:java:<source id>:<manga id>"
// and its chapters under that plus ":<chapter id>", so the store, the Library and the History need no new shape.
#pragma once
#include "suwayomi.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace bp
{
	class MangaJava
	{
	public:
		// the host's channel: the request body in, its serial out (0: nothing was sent)
		using Submit = std::function<uint64_t(const std::string& body)>;
		// whether the engine answers now (unset: always). An install / update / uninstall asked while it doesn't is
		// kept here, in order, and sent by sendQueued() once it does: the host's own wait for a starting engine has a
		// deadline and is dropped by a stop, and a request lost there left its row saying Installing… (the handheld)
		using Ready = std::function<bool()>;
		// milliseconds, any origin (unset: no expiry), for expire()
		using Clock = std::function<int64_t()>;

		struct Repo
		{
			std::string url, name;
			bool added = false;		// the store answered
			bool queued = false;	// added while the engine wasn't there: sent when it runs (sendQueued), kept meanwhile
			std::string error;
		};
		struct Source
		{
			std::string id, name, lang, displayName, pkgName;
			bool supportsLatest = false, nsfw = false;
			// the name a Browse row shows: one row per source and language
			std::string title() const { return displayName.empty() ? (name.empty() ? id : name) : displayName; }
		};
		struct Event
		{
			enum Kind { RepoAdded, RepoFailed, ExtensionsLoaded, ExtensionsFailed, Installed, Updated, Uninstalled, ActionFailed };
			Kind kind = RepoAdded;
			std::string url, pkg, message;
		};

		// "http(s)://…/index.pb" or "…/index.min.json": a Tachiyomi / Mihon repository index (the Lua
		// repositories' JSON index is a different file and keeps its own path)
		static bool isRepoUrl(const std::string& url);
		static bool isSourceId(const std::string& fullId) { return fullId.rfind("java:", 0) == 0; }
		static std::string sourceId(const std::string& suwayomiSourceId) { return "java:" + suwayomiSourceId; }
		static std::string suwayomiSourceId(const std::string& fullId)
		{
			return isSourceId(fullId) ? fullId.substr(5) : std::string();
		}

		void setSubmit(Submit submit) { submit_ = std::move(submit); }
		void setReady(Ready ready) { ready_ = std::move(ready); }
		void setClock(Clock clock) { clock_ = std::move(clock); }
		// <stateDir>/manga/java/repos.tsv and sources.tsv. A missing or damaged file is an empty list, never a
		// failure to start (as the Lua repositories' store).
		bool load(const std::string& stateDir);
		bool save();

		const std::vector<Repo>& repos() const { return repos_; }
		const std::vector<Source>& sources() const { return sources_; }
		const std::vector<SuwayomiExt>& extensions() const { return extensions_; }
		bool extensionsKnown() const { return extensionsKnown_; }
		bool hasRepos() const { return !repos_.empty(); }
		bool hasQueued() const;
		// the repositories added and the actions asked while the engine wasn't there, sent now in the order asked
		// (the engine is answering)
		void sendQueued();
		// an action waiting for the engine (the row says Waiting for the engine…)
		bool queued(const std::string& pkgName) const;
		bool hasQueuedActions() const { return !queuedActions_.empty(); }
		// the engine won't come (it failed, or was stopped): the waiting actions end, each with an ActionFailed saying why
		void failQueued(const std::string& why);
		// an action that waited too long for the engine, or whose answer never came, ends with an ActionFailed (true:
		// something ended). Called every pass; needs the Clock.
		bool expire();
		static constexpr int64_t QUEUED_MS = 180000;	// the engine's start is about 25 s, its own deadline 150 s
		static constexpr int64_t SENT_MS = 270000;		// the host waits up to 120 s for the engine, then 120 s of curl
		// the source a Browse row is for (nullptr: it isn't installed any more)
		const Source* source(const std::string& fullId) const;
		const SuwayomiExt* extension(const std::string& pkgName) const;

		// Actions. False and `error` says why when the request couldn't even be queued; the answer arrives later as
		// an Event and through onAnswer().
		bool addRepo(const std::string& url, std::string& error);
		bool removeRepo(const std::string& url, std::string& error);
		void refreshExtensions();
		// the installed extensions' sources asked of the engine once it runs (the card's sources.tsv is a copy: lost or
		// stale, Browse would miss sources the engine has); asked once, again after a failed answer (3 tries)
		void syncInstalled();
		bool syncWanted() const { return !syncAsked_ && syncTries_ < 3; }
		bool install(const std::string& pkgName, std::string& error);
		bool update(const std::string& pkgName, std::string& error);
		bool uninstall(const std::string& pkgName, std::string& error);
		// true while an install / update / uninstall for that package is out (the row says Installing…)
		bool busy(const std::string& pkgName) const;
		bool refreshPending() const;

		// The host's answer. True: it was one of this manager's requests (and is handled); false: the UI's own.
		bool onAnswer(uint64_t serial, const std::string& body, const std::string& error);
		std::vector<Event> takeEvents();

		uint64_t serial() const { return serial_; }	// grows with every change the pages draw
		// the engine stopped (the extension turned off, a video, quitting): requests out are forgotten (their answers
		// would never come); an install / update / uninstall among them, or waiting, ends with an ActionFailed saying why
		void cancelAll(const std::string& why = "the manga engine stopped");

	private:
		enum class Kind { AddStore, RemoveStore, FetchExtensions, Install, Update, Uninstall, SyncInstalled };
		struct Pending
		{
			Kind kind = Kind::AddStore;
			std::string url, pkg;
			int64_t at = 0;	// when it was sent (the Clock)
		};
		// an install / update / uninstall: asked of the engine now, or kept until it answers
		bool action(Kind kind, const std::string& pkgName, std::string& error);
		bool sendAction(Kind kind, const std::string& pkgName);
		void failAction(Kind kind, const std::string& pkgName, const std::string& why);
		int64_t now() const { return clock_ ? clock_() : 0; }

		bool submitRequest(Kind kind, const std::string& body, const std::string& url, const std::string& pkg);
		std::string dir() const { return stateDir_.empty() ? std::string() : stateDir_ + "/manga/java"; }
		std::string repoFile() const { return dir() + "/repos.tsv"; }
		std::string sourceFile() const { return dir() + "/sources.tsv"; }
		void addEvent(Event::Kind kind, const std::string& url, const std::string& pkg, const std::string& message);
		SuwayomiExt* extensionAt(const std::string& pkgName);
		void addSources(const std::vector<SuwayomiSource>& sources, const std::string& pkgName);
		void dropSources(const std::string& pkgName);
		std::string extensionName(const std::string& pkgName) const;

		std::string stateDir_;
		Submit submit_;
		Ready ready_;
		Clock clock_;
		std::vector<Pending> queuedActions_;	// waiting for the engine, in the order asked (at: when asked)
		std::vector<Repo> repos_;
		std::vector<Source> sources_;
		std::vector<SuwayomiExt> extensions_;
		bool extensionsKnown_ = false;
		bool syncAsked_ = false;
		int syncTries_ = 0;
		// the sources as the engine has them, from a list of extensions: complete, what isn't in it has none
		void adoptSources(const std::vector<SuwayomiExt>& exts, bool complete);
		std::map<uint64_t, Pending> pending_;
		std::vector<Event> events_;
		uint64_t serial_ = 0;
	};
}
