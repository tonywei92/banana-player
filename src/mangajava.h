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
		// the repositories added while the engine wasn't there, sent now (the engine is answering)
		void sendQueued();
		// the source a Browse row is for (nullptr: it isn't installed any more)
		const Source* source(const std::string& fullId) const;
		const SuwayomiExt* extension(const std::string& pkgName) const;

		// Actions. False and `error` says why when the request couldn't even be queued; the answer arrives later as
		// an Event and through onAnswer().
		bool addRepo(const std::string& url, std::string& error);
		bool removeRepo(const std::string& url, std::string& error);
		void refreshExtensions();
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
		// the extension turned off: requests still queued are forgotten (their answers would never come)
		void cancelAll();

	private:
		enum class Kind { AddStore, RemoveStore, FetchExtensions, Install, Update, Uninstall };
		struct Pending
		{
			Kind kind = Kind::AddStore;
			std::string url, pkg;
		};

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
		std::vector<Repo> repos_;
		std::vector<Source> sources_;
		std::vector<SuwayomiExt> extensions_;
		bool extensionsKnown_ = false;
		std::map<uint64_t, Pending> pending_;
		std::vector<Event> events_;
		uint64_t serial_ = 0;
	};
}
