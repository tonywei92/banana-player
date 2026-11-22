// The Java extensions' repositories, extensions and sources (see mangajava.h). Pure: every answer is a string the
// host took from Suwayomi, and every request body comes from suwayomi.*.
#include "mangajava.h"
#include "filesave.h"
#include <iterator>
#include "mangarepo.h"	// validRepoUrl
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace bp
{
	namespace
	{
		bool isDir(const std::string& p)
		{
			struct stat st;
			return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
		}

		bool ensureDir(const std::string& path)
		{
			if (path.empty()) return false;
			if (isDir(path)) return true;
			size_t slash = path.find_last_of('/');
			if (slash != std::string::npos && slash > 0 && !ensureDir(path.substr(0, slash))) return false;
			return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
		}

		// a TSV field: the tabs and line ends would break the line a reader splits on
		std::string field(const std::string& s)
		{
			std::string out = s;
			for (char& c : out)
				if (c == '\t' || c == '\n' || c == '\r') c = ' ';
			return out;
		}

		std::vector<std::string> splitTabs(const std::string& line)
		{
			std::vector<std::string> out;
			size_t at = 0;
			while (true)
			{
				size_t tab = line.find('\t', at);
				if (tab == std::string::npos)
				{
					out.push_back(line.substr(at));
					return out;
				}
				out.push_back(line.substr(at, tab - at));
				at = tab + 1;
			}
		}

		bool endsWith(const std::string& s, const char* tail)
		{
			size_t n = std::char_traits<char>::length(tail);
			return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
		}

		// the name a repository shows until the store names itself: the host and its first two path pieces
		std::string repoNameFromUrl(const std::string& url)
		{
			std::string rest = url;
			size_t scheme = rest.find("://");
			if (scheme != std::string::npos) rest = rest.substr(scheme + 3);
			size_t cut = rest.find_first_of("?#");
			if (cut != std::string::npos) rest.resize(cut);
			while (!rest.empty() && rest.back() == '/') rest.pop_back();
			std::vector<std::string> parts;
			size_t at = 0;
			while (at <= rest.size())
			{
				size_t slash = rest.find('/', at);
				if (slash == std::string::npos) slash = rest.size();
				if (slash > at) parts.push_back(rest.substr(at, slash - at));
				if (slash == rest.size()) break;
				at = slash + 1;
			}
			if (parts.empty()) return url;
			if (parts.size() == 1) return parts[0];
			return parts[0] + "/" + parts[1];
		}
	}

	bool MangaJava::isRepoUrl(const std::string& url)
	{
		if (!validRepoUrl(url)) return false;
		// a Tachiyomi / Mihon index; a Lua repository's index.json is a different file and stays with MangaRepos
		return endsWith(url, "/index.pb") || endsWith(url, "/index.min.json");
	}

	bool MangaJava::load(const std::string& stateDir)
	{
		stateDir_ = stateDir;
		repos_.clear();
		sources_.clear();
		extensions_.clear();
		extensionsKnown_ = false;
		pending_.clear();
		events_.clear();
		if (dir().empty()) return false;
		// the last list the engine gave, shown until it gives a new one (extensionsKnown_ stays false: it is asked again)
		{
			std::ifstream in(dir() + "/extensions.json", std::ios::binary);
			if (in)
			{
				std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				std::vector<SuwayomiExt> cached;
				std::string err;
				if (suwayomiParseExtensions(body, cached, err)) extensions_ = std::move(cached);
			}
		}
		{
			std::ifstream in(repoFile());
			std::string line;
			while (std::getline(in, line))
			{
				if (line.empty()) continue;
				auto f = splitTabs(line);
				if (f.size() < 2 || f[0].empty()) continue;
				Repo r;
				r.url = f[0];
				r.name = f[1];
				r.queued = f.size() >= 3 && f[2] == "queued";
				r.added = !r.queued;
				repos_.push_back(std::move(r));
			}
		}
		{
			std::ifstream in(sourceFile());
			std::string line;
			while (std::getline(in, line))
			{
				if (line.empty()) continue;
				auto f = splitTabs(line);
				if (f.size() < 6 || f[0].empty()) continue;
				Source s;
				s.id = f[0];
				s.name = f[1];
				s.lang = f[2];
				s.displayName = f[3];
				s.pkgName = f[4];
				s.supportsLatest = f[5] == "1";
				s.nsfw = f.size() > 6 && f[6] == "1";
				sources_.push_back(std::move(s));
			}
		}
		serial_++;
		return true;
	}

	bool MangaJava::save()
	{
		if (dir().empty() || !ensureDir(dir())) return false;
		auto writeFile = [](const std::string& path, const std::string& text) { return writeFileDurable(path, text); };
		std::string repos;
		for (const Repo& r : repos_)
			if (r.added || r.queued) repos += field(r.url) + "\t" + field(r.name) + (r.queued ? "\tqueued" : "") + "\n";
		std::string sources;
		for (const Source& s : sources_)
			sources += field(s.id) + "\t" + field(s.name) + "\t" + field(s.lang) + "\t" + field(s.displayName) + "\t" +
				field(s.pkgName) + "\t" + (s.supportsLatest ? "1" : "0") + "\t" + (s.nsfw ? "1" : "0") + "\n";
		bool ok = writeFile(repoFile(), repos);
		return writeFile(sourceFile(), sources) && ok;
	}

	const MangaJava::Source* MangaJava::source(const std::string& fullId) const
	{
		std::string id = suwayomiSourceId(fullId);
		for (const Source& s : sources_)
			if (s.id == id) return &s;
		return nullptr;
	}

	const SuwayomiExt* MangaJava::extension(const std::string& pkgName) const
	{
		for (const SuwayomiExt& e : extensions_)
			if (e.pkgName == pkgName) return &e;
		return nullptr;
	}

	SuwayomiExt* MangaJava::extensionAt(const std::string& pkgName)
	{
		for (SuwayomiExt& e : extensions_)
			if (e.pkgName == pkgName) return &e;
		return nullptr;
	}

	bool MangaJava::submitRequest(Kind kind, const std::string& body, const std::string& url, const std::string& pkg)
	{
		if (!submit_) return false;
		uint64_t serial = submit_(body);
		if (!serial) return false;
		Pending p;
		p.kind = kind;
		p.url = url;
		p.pkg = pkg;
		p.at = now();
		pending_[serial] = p;
		serial_++;
		return true;
	}

	bool MangaJava::addRepo(const std::string& url, std::string& error)
	{
		error.clear();
		if (!isRepoUrl(url))
		{
			error = "not a Tachiyomi repository index (index.pb or index.min.json)";
			return false;
		}
		for (const Repo& r : repos_)
			if (r.url == url)
			{
				error = "already added";
				return false;
			}
		Repo r;
		r.url = url;
		r.name = repoNameFromUrl(url);
		// no engine to ask (not installed, or not up): kept, and sent once it runs; the engine's answer then keeps it
		// or takes it away with its reason, as for one added while it runs
		if (!submitRequest(Kind::AddStore, suwayomiAddExtensionStore(url), url, "")) r.queued = true;
		repos_.push_back(r);
		serial_++;
		return true;
	}

	bool MangaJava::hasQueued() const
	{
		if (!queuedActions_.empty()) return true;
		for (const Repo& r : repos_)
			if (r.queued) return true;
		return false;
	}

	void MangaJava::sendQueued()
	{
		for (Repo& r : repos_)
			if (r.queued && submitRequest(Kind::AddStore, suwayomiAddExtensionStore(r.url), r.url, "")) r.queued = false;
		std::vector<Pending> actions;
		actions.swap(queuedActions_);
		for (const Pending& a : actions)
			if (!sendAction(a.kind, a.pkg)) failAction(a.kind, a.pkg, "couldn't reach the extensions");
		if (!actions.empty()) serial_++;
	}

	bool MangaJava::queued(const std::string& pkgName) const
	{
		for (const Pending& a : queuedActions_)
			if (a.pkg == pkgName) return true;
		return false;
	}

	void MangaJava::failQueued(const std::string& why)
	{
		std::vector<Pending> actions;
		actions.swap(queuedActions_);
		for (const Pending& a : actions) failAction(a.kind, a.pkg, why);
		if (!actions.empty()) serial_++;
	}

	bool MangaJava::expire()
	{
		if (!clock_) return false;
		int64_t t = now();
		bool changed = false;
		for (auto it = queuedActions_.begin(); it != queuedActions_.end();)
		{
			if (t - it->at >= QUEUED_MS)
			{
				failAction(it->kind, it->pkg, "the manga engine didn't start in time");
				it = queuedActions_.erase(it);
				changed = true;
			}
			else ++it;
		}
		for (auto it = pending_.begin(); it != pending_.end();)
		{
			const Pending& p = it->second;
			bool act = p.kind == Kind::Install || p.kind == Kind::Update || p.kind == Kind::Uninstall;
			if (act && t - p.at >= SENT_MS)
			{
				// (a late answer is then nobody's: the next sync of the installed sources brings what it did)
				failAction(p.kind, p.pkg, "the manga engine didn't answer");
				it = pending_.erase(it);
				changed = true;
			}
			else ++it;
		}
		if (changed) serial_++;
		return changed;
	}

	bool MangaJava::removeRepo(const std::string& url, std::string& error)
	{
		error.clear();
		auto it = std::find_if(repos_.begin(), repos_.end(), [&](const Repo& r) { return r.url == url; });
		if (it == repos_.end())
		{
			error = "no such repository";
			return false;
		}
		// one the engine never had: nothing to tell it
		if (it->queued || !it->added)
		{
			repos_.erase(it);
			serial_++;
			return true;
		}
		if (!submitRequest(Kind::RemoveStore, suwayomiRemoveExtensionStore(url), url, ""))
		{
			error = "couldn't reach the extensions";
			return false;
		}
		repos_.erase(it);
		serial_++;
		return true;
	}

	void MangaJava::refreshExtensions()
	{
		if (refreshPending()) return;
		if (!submitRequest(Kind::FetchExtensions, suwayomiFetchExtensions(), "", ""))
			addEvent(Event::ExtensionsFailed, "", "", "Couldn't reach the extensions");
	}

	bool MangaJava::refreshPending() const
	{
		for (const auto& kv : pending_)
			if (kv.second.kind == Kind::FetchExtensions) return true;
		return false;
	}

	namespace
	{
		const char* actionVerb(bool install, bool update) { return install ? "install" : update ? "update" : "uninstall"; }
	}

	bool MangaJava::install(const std::string& pkgName, std::string& error) { return action(Kind::Install, pkgName, error); }
	bool MangaJava::update(const std::string& pkgName, std::string& error) { return action(Kind::Update, pkgName, error); }
	bool MangaJava::uninstall(const std::string& pkgName, std::string& error) { return action(Kind::Uninstall, pkgName, error); }

	bool MangaJava::action(Kind kind, const std::string& pkgName, std::string& error)
	{
		error.clear();
		const char* verb = actionVerb(kind == Kind::Install, kind == Kind::Update);
		if (pkgName.empty()) { error = "no extension"; return false; }
		if (busy(pkgName)) { error = "already being changed"; return false; }
		if (ready_ && !ready_())
		{
			Pending a;
			a.kind = kind;
			a.pkg = pkgName;
			a.at = now();
			queuedActions_.push_back(a);
			serial_++;
			fprintf(stderr, "sources: %s %s queued until the engine is ready\n", verb, pkgName.c_str());
			return true;
		}
		if (!sendAction(kind, pkgName))
		{
			error = "couldn't reach the extensions";
			fprintf(stderr, "sources: %s %s failed: %s\n", verb, pkgName.c_str(), error.c_str());
			return false;
		}
		return true;
	}

	bool MangaJava::sendAction(Kind kind, const std::string& pkgName)
	{
		std::string body = kind == Kind::Install ? suwayomiInstallExtension(pkgName)
			: kind == Kind::Update ? suwayomiUpdateExtension(pkgName) : suwayomiUninstallExtension(pkgName);
		if (!submitRequest(kind, body, "", pkgName)) return false;
		fprintf(stderr, "sources: %s %s sent\n", actionVerb(kind == Kind::Install, kind == Kind::Update), pkgName.c_str());
		return true;
	}

	void MangaJava::failAction(Kind kind, const std::string& pkgName, const std::string& why)
	{
		bool inst = kind == Kind::Install, upd = kind == Kind::Update;
		fprintf(stderr, "sources: %s %s failed: %s\n", actionVerb(inst, upd), pkgName.c_str(), why.c_str());
		addEvent(Event::ActionFailed, "", pkgName, extensionName(pkgName) + (inst ? " wasn't installed: " :
			upd ? " wasn't updated: " : " wasn't removed: ") + why);
	}

	bool MangaJava::busy(const std::string& pkgName) const
	{
		if (queued(pkgName)) return true;
		for (const auto& kv : pending_)
		{
			const Pending& p = kv.second;
			if (p.pkg == pkgName && (p.kind == Kind::Install || p.kind == Kind::Update || p.kind == Kind::Uninstall))
				return true;
		}
		return false;
	}

	void MangaJava::addSources(const std::vector<SuwayomiSource>& list, const std::string& pkgName)
	{
		dropSources(pkgName);
		const SuwayomiExt* ext = extension(pkgName);
		bool pkgNsfw = ext && ext->isNsfw;
		for (const SuwayomiSource& s : list)
		{
			Source out;
			out.id = s.id;
			out.name = s.name;
			out.lang = s.lang;
			out.displayName = s.displayName;
			out.pkgName = pkgName;
			out.supportsLatest = s.supportsLatest;
			out.nsfw = s.isNsfw || pkgNsfw;
			// one source id is one row: an extension reinstalled under a different package still names it once
			sources_.erase(std::remove_if(sources_.begin(), sources_.end(),
				[&](const Source& old) { return old.id == out.id; }), sources_.end());
			sources_.push_back(std::move(out));
		}
		std::sort(sources_.begin(), sources_.end(), [](const Source& a, const Source& b) {
			if (a.name != b.name) return a.name < b.name;
			return a.lang < b.lang;
		});
	}

	void MangaJava::syncInstalled()
	{
		if (syncAsked_) return;
		syncTries_++;
		syncAsked_ = submitRequest(Kind::SyncInstalled, suwayomiFetchInstalledSources(), "", "");
	}

	void MangaJava::adoptSources(const std::vector<SuwayomiExt>& exts, bool complete)
	{
		size_t before = sources_.size();
		std::vector<std::string> installed;
		for (const SuwayomiExt& e : exts)
		{
			if (!e.sourcesKnown) continue;
			if (e.isInstalled)
			{
				addSources(e.sources, e.pkgName);
				installed.push_back(e.pkgName);
				if (e.sources.empty())
					fprintf(stderr, "manga: extension %s is installed but the engine couldn't load it (no sources)\n", e.pkgName.c_str());
			}
			else dropSources(e.pkgName);
		}
		// the installed list is all of them: a source whose extension isn't there any more goes
		if (complete)
			sources_.erase(std::remove_if(sources_.begin(), sources_.end(), [&](const Source& s) {
				return std::find(installed.begin(), installed.end(), s.pkgName) == installed.end();
			}), sources_.end());
		fprintf(stderr, "manga: extension sources %zu -> %zu, from the engine (%zu installed)\n", before, sources_.size(), installed.size());
		serial_++;
		save();
	}

	void MangaJava::dropSources(const std::string& pkgName)
	{
		sources_.erase(std::remove_if(sources_.begin(), sources_.end(),
			[&](const Source& s) { return s.pkgName == pkgName; }), sources_.end());
	}

	std::string MangaJava::extensionName(const std::string& pkgName) const
	{
		const SuwayomiExt* e = extension(pkgName);
		return e && !e->name.empty() ? e->name : pkgName;
	}

	void MangaJava::addEvent(Event::Kind kind, const std::string& url, const std::string& pkg, const std::string& message)
	{
		Event e;
		e.kind = kind;
		e.url = url;
		e.pkg = pkg;
		e.message = message;
		events_.push_back(std::move(e));
	}

	bool MangaJava::onAnswer(uint64_t serial, const std::string& body, const std::string& error)
	{
		auto it = pending_.find(serial);
		if (it == pending_.end()) return false;
		Pending p = it->second;
		pending_.erase(it);
		serial_++;
		std::string parseError;
		switch (p.kind)
		{
		case Kind::AddStore:
		{
			std::string name;
			if (!error.empty() || !suwayomiParseAddExtensionStore(body, parseError, &name))
			{
				repos_.erase(std::remove_if(repos_.begin(), repos_.end(),
					[&](const Repo& r) { return r.url == p.url; }), repos_.end());
				if (parseError.empty() && !error.empty()) parseError = error;
				addEvent(Event::RepoFailed, p.url, "", parseError.empty() ? "the store wasn't added" : parseError);
				break;
			}
			for (Repo& r : repos_)
				if (r.url == p.url)
				{
					r.added = true;
					if (!name.empty()) r.name = name;
					addEvent(Event::RepoAdded, r.url, "", r.name);
					break;
				}
			break;
		}
		case Kind::RemoveStore:
			if (!error.empty() || !suwayomiParseRemoveExtensionStore(body, parseError))
			{
				if (parseError.empty() && !error.empty()) parseError = error;
				addEvent(Event::ActionFailed, p.url, "", parseError.empty() ? "the store wasn't removed" : parseError);
			}
			break;
		case Kind::FetchExtensions:
		{
			std::vector<SuwayomiExt> out;
			if (!error.empty() || !suwayomiParseExtensions(body, out, parseError))
			{
				if (parseError.empty() && !error.empty()) parseError = error;
				addEvent(Event::ExtensionsFailed, "", "", parseError.empty() ? "the extensions didn't come" : parseError);
				break;
			}
			extensions_ = std::move(out);
			extensionsKnown_ = true;
			// kept on the card: the next start lists them before the manga engine is up (installing still waits for it)
			{
				writeFileAtomic(dir() + "/extensions.json", body);	// (a list fetched again: not worth a sync)
			}
			adoptSources(extensions_, false);
			// an adult extension's sources are all adult, even when the source nodes don't repeat the flag
			for (Source& s : sources_)
				if (const SuwayomiExt* e = extension(s.pkgName))
					if (e->isNsfw) s.nsfw = true;
			addEvent(Event::ExtensionsLoaded, "", "", "");
			break;
		}
		case Kind::SyncInstalled:
		{
			std::vector<SuwayomiExt> out;
			if (!error.empty() || !suwayomiParseInstalledSources(body, out, parseError))
			{
				syncAsked_ = false;	// asked again (syncWanted), a few times at most
				fprintf(stderr, "manga: the installed extensions' sources didn't come: %s\n", (parseError.empty() ? error : parseError).c_str());
				break;
			}
			adoptSources(out, true);
			break;
		}
		case Kind::Install:
		case Kind::Update:
		{
			std::vector<SuwayomiSource> list;
			if (!error.empty() || !suwayomiParseInstallResult(body, list, parseError))
			{
				if (parseError.empty() && !error.empty()) parseError = error;
				fprintf(stderr, "sources: %s %s failed: %s\n", p.kind == Kind::Install ? "install" : "update",
					p.pkg.c_str(), parseError.c_str());
				addEvent(Event::ActionFailed, "", p.pkg,
					parseError.empty() ? "the extension couldn't be installed" : parseError);
				break;
			}
			SuwayomiExt* e = extensionAt(p.pkg);
			if (!e)
			{
				SuwayomiExt made;
				made.pkgName = p.pkg;
				extensions_.push_back(made);
				e = &extensions_.back();
			}
			e->isInstalled = true;
			e->hasUpdate = false;
			// the extension's own sources too: a list fetched before the install says "none, known" for it, and
			// installed with none reads as Can't load until the list is fetched again
			e->sources = list;
			e->sourcesKnown = true;
			addSources(list, p.pkg);
			fprintf(stderr, "sources: %s %s done (%zu sources)\n", p.kind == Kind::Install ? "install" : "update",
				p.pkg.c_str(), list.size());
			addEvent(p.kind == Kind::Install ? Event::Installed : Event::Updated, "", p.pkg, extensionName(p.pkg));
			break;
		}
		case Kind::Uninstall:
			if (!error.empty() || suwayomiCheckErrors(body, parseError))
			{
				if (parseError.empty() && !error.empty()) parseError = error;
				fprintf(stderr, "sources: uninstall %s failed: %s\n", p.pkg.c_str(), parseError.c_str());
				addEvent(Event::ActionFailed, "", p.pkg,
					parseError.empty() ? "the extension couldn't be removed" : parseError);
				break;
			}
			if (SuwayomiExt* e = extensionAt(p.pkg))
			{
				e->isInstalled = false;
				e->hasUpdate = false;
			}
			dropSources(p.pkg);
			fprintf(stderr, "sources: uninstall %s done\n", p.pkg.c_str());
			addEvent(Event::Uninstalled, "", p.pkg, extensionName(p.pkg));
			break;
		}
		return true;
	}

	std::vector<MangaJava::Event> MangaJava::takeEvents()
	{
		std::vector<Event> out;
		out.swap(events_);
		return out;
	}

	void MangaJava::cancelAll(const std::string& why)
	{
		failQueued(why);
		if (pending_.empty()) return;
		for (const auto& kv : pending_)
		{
			const Pending& p = kv.second;
			if (p.kind == Kind::Install || p.kind == Kind::Update || p.kind == Kind::Uninstall) failAction(p.kind, p.pkg, why);
		}
		pending_.clear();
		serial_++;
	}
}
