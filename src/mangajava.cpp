// The Java extensions' repositories, extensions and sources (see mangajava.h). Pure: every answer is a string the
// host took from Suwayomi, and every request body comes from suwayomi.*.
#include "mangajava.h"
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
				r.added = true;
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
		auto writeFile = [](const std::string& path, const std::string& text) {
			std::string tmp = path + ".tmp";
			FILE* f = fopen(tmp.c_str(), "wb");
			if (!f) return false;
			bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
			if (fclose(f) != 0) ok = false;
			if (!ok || rename(tmp.c_str(), path.c_str()) != 0)
			{
				unlink(tmp.c_str());
				return false;
			}
			return true;
		};
		std::string repos;
		for (const Repo& r : repos_)
			if (r.added) repos += field(r.url) + "\t" + field(r.name) + "\n";
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
		repos_.push_back(r);
		if (!submitRequest(Kind::AddStore, suwayomiAddExtensionStore(url), url, ""))
		{
			repos_.pop_back();
			error = "couldn't reach the Java extensions";
			return false;
		}
		return true;
	}

	bool MangaJava::removeRepo(const std::string& url, std::string& error)
	{
		error.clear();
		auto it = std::find_if(repos_.begin(), repos_.end(), [&](const Repo& r) { return r.url == url; });
		if (it == repos_.end())
		{
			error = "not a Java repository";
			return false;
		}
		if (!submitRequest(Kind::RemoveStore, suwayomiRemoveExtensionStore(url), url, ""))
		{
			error = "couldn't reach the Java extensions";
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
			addEvent(Event::ExtensionsFailed, "", "", "Couldn't reach the Java extensions");
	}

	bool MangaJava::refreshPending() const
	{
		for (const auto& kv : pending_)
			if (kv.second.kind == Kind::FetchExtensions) return true;
		return false;
	}

	bool MangaJava::install(const std::string& pkgName, std::string& error)
	{
		error.clear();
		if (pkgName.empty()) { error = "no extension"; return false; }
		if (busy(pkgName)) { error = "already being changed"; return false; }
		if (!submitRequest(Kind::Install, suwayomiInstallExtension(pkgName), "", pkgName))
		{
			error = "couldn't reach the Java extensions";
			return false;
		}
		return true;
	}

	bool MangaJava::update(const std::string& pkgName, std::string& error)
	{
		error.clear();
		if (pkgName.empty()) { error = "no extension"; return false; }
		if (busy(pkgName)) { error = "already being changed"; return false; }
		if (!submitRequest(Kind::Update, suwayomiUpdateExtension(pkgName), "", pkgName))
		{
			error = "couldn't reach the Java extensions";
			return false;
		}
		return true;
	}

	bool MangaJava::uninstall(const std::string& pkgName, std::string& error)
	{
		error.clear();
		if (pkgName.empty()) { error = "no extension"; return false; }
		if (busy(pkgName)) { error = "already being changed"; return false; }
		if (!submitRequest(Kind::Uninstall, suwayomiUninstallExtension(pkgName), "", pkgName))
		{
			error = "couldn't reach the Java extensions";
			return false;
		}
		return true;
	}

	bool MangaJava::busy(const std::string& pkgName) const
	{
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
			// an adult extension's sources are all adult, even when the source nodes don't repeat the flag
			for (Source& s : sources_)
				if (const SuwayomiExt* e = extension(s.pkgName))
					if (e->isNsfw) s.nsfw = true;
			addEvent(Event::ExtensionsLoaded, "", "", "");
			break;
		}
		case Kind::Install:
		case Kind::Update:
		{
			std::vector<SuwayomiSource> list;
			if (!error.empty() || !suwayomiParseInstallResult(body, list, parseError))
			{
				if (parseError.empty() && !error.empty()) parseError = error;
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
			addSources(list, p.pkg);
			addEvent(p.kind == Kind::Install ? Event::Installed : Event::Updated, "", p.pkg, extensionName(p.pkg));
			break;
		}
		case Kind::Uninstall:
			if (!error.empty() || suwayomiCheckErrors(body, parseError))
			{
				if (parseError.empty() && !error.empty()) parseError = error;
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

	void MangaJava::cancelAll()
	{
		if (pending_.empty()) return;
		pending_.clear();
		serial_++;
	}
}
