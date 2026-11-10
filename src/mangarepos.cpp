// See mangarepos.h. The loop drives everything: add/install/refresh only queue a fetch, and every answer is taken
// from MangaFetch on the main thread and handed to onFetched. The one thing that is checked here, not in the Lua host,
// is the cheap half of design 5.3's install rule: the size and the SHA-256 against the index. The script's own shape
// is the injected validator's business (design 5.4).
#include "mangarepos.h"
#include "playlists.h"	// writeFileAtomic: the tmp-then-rename every store writes with
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace bp
{
	namespace
	{
		const char* REPOS_HEADER = "# banana-player manga repositories v1";
		const char* SOURCES_HEADER = "# banana-player manga sources v1";

		// The same field escaping as mangastore.cpp: a line stays a line and a field stays a field, even in a name
		// an index handed us.
		std::string escapeField(const std::string& s)
		{
			std::string o;
			o.reserve(s.size());
			for (char c : s)
			{
				if (c == '\t') o += "\\t";
				else if (c == '\n') o += "\\n";
				else if (c == '\r') o += "\\r";
				else if (c == '\\') o += "\\\\";
				else o += c;
			}
			return o;
		}

		std::string unescapeField(const std::string& s)
		{
			std::string o;
			o.reserve(s.size());
			for (size_t i = 0; i < s.size(); i++)
			{
				if (s[i] != '\\' || i + 1 >= s.size())
				{
					o += s[i];
					continue;
				}
				char n = s[++i];
				if (n == 't') o += '\t';
				else if (n == 'n') o += '\n';
				else if (n == 'r') o += '\r';
				else if (n == '\\') o += '\\';
				else
				{
					o += '\\';
					o += n;
				}
			}
			return o;
		}

		std::vector<std::string> splitTabs(const std::string& line)
		{
			std::vector<std::string> f;
			size_t pos = 0;
			for (;;)
			{
				size_t t = line.find('\t', pos);
				if (t == std::string::npos)
				{
					f.push_back(line.substr(pos));
					break;
				}
				f.push_back(line.substr(pos, t - pos));
				pos = t + 1;
			}
			return f;
		}

		bool toInt(const std::string& s, int64_t& out)
		{
			if (s.empty()) return false;
			char* end = nullptr;
			long long v = strtoll(s.c_str(), &end, 10);
			if (!end || *end) return false;
			out = v;
			return true;
		}

		bool toBool(const std::string& s, bool& out)
		{
			if (s == "1") { out = true; return true; }
			if (s == "0") { out = false; return true; }
			return false;
		}

		bool readWhole(const std::string& path, std::string& out)
		{
			FILE* f = fopen(path.c_str(), "rb");
			if (!f) return false;
			out.clear();
			char buf[4096];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
			fclose(f);
			return true;
		}

		// mkdir -p, best effort: a write that can't happen is reported by the write itself.
		bool mkdirP(const std::string& path)
		{
			if (path.empty()) return false;
			std::string cur;
			for (size_t i = 0; i < path.size(); i++)
			{
				cur += path[i];
				if (path[i] == '/' && cur.size() > 1) mkdir(cur.c_str(), 0755);
			}
			mkdir(path.c_str(), 0755);
			struct stat st;
			return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
		}

		// A file name's safety: the index's own id rule (mangarepo.cpp), repeated because these ids come from a file
		// the user can edit. A "/" or ".." here would write outside manga/sources.
		bool validId(const std::string& id)
		{
			if (id.empty() || id.size() > 64) return false;
			bool any = false;
			for (char c : id)
			{
				if (!(c >= 'a' && c <= 'z' || c >= '0' && c <= '9' || c == '.' || c == '_' || c == '-')) return false;
				if (c != '.') any = true;
			}
			return any;
		}

		bool isHex12(const std::string& h)
		{
			if (h.size() != 12) return false;
			for (char c : h)
				if (!isxdigit((unsigned char)c)) return false;
			return true;
		}

		// MangaFetch::Result carries no response headers, so the etag stored for If-None-Match is our own strong
		// entity tag over the index bytes. A server that echoes those tags gives design 5.3's 304; one that doesn't
		// simply sends the index again, which is only wasted bytes, never a wrong cache.
		std::string indexEtag(const std::string& body)
		{
			std::string h = sha256Hex((const uint8_t*)body.data(), body.size());
			return h.empty() ? std::string() : "\"" + h + "\"";
		}

		// MangaFetch says a capped transfer this way (mangafetch.cpp's finish); kept in one place so its wording is
		// not spread over the manager.
		bool overLimit(const std::string& error)
		{
			return error.rfind("the response is over", 0) == 0;
		}

		std::string failureOf(const MangaFetch::Result& r)
		{
			if (!r.error.empty()) return r.error;
			if (r.status != 0) return "HTTP " + std::to_string(r.status);
			return "no answer";
		}
	}


	MangaRepos::MangaRepos(FetchApi& fetch, Validator validator) : fetch_(fetch), validator_(std::move(validator))
	{
	}

	MangaRepos::~MangaRepos()
	{
		// The results would only sit in MangaFetch's queue; the fetcher is never destroyed, so this is the polite
		// way to say nothing more is wanted.
		for (const auto& kv : pending_) fetch_.cancel(kv.first);
	}

	void MangaRepos::setClock(std::function<int64_t()> clock)
	{
		clock_ = std::move(clock);
	}

	int64_t MangaRepos::now() const
	{
		return clock_ ? clock_() : (int64_t)time(nullptr);
	}

	std::string MangaRepos::repoHash(const std::string& url)
	{
		std::string h = sha256Hex((const uint8_t*)url.data(), url.size());
		return h.substr(0, 12);
	}

	std::string MangaRepos::fullId(const std::string& repoHash, const std::string& id)
	{
		return repoHash + "/" + id;
	}

	bool MangaRepos::load(const std::string& stateDir)
	{
		// a load replaces everything: requests still on their way would only be answered for a state that is gone
		for (const auto& kv : pending_) fetch_.cancel(kv.first);
		repos_.clear();
		installed_.clear();
		pending_.clear();
		events_.clear();
		stateDir_ = stateDir;
		if (stateDir_.empty()) return false;

		std::string text;
		if (readWhole(mangaDir() + "/repos.tsv", text))
		{
			std::istringstream in(text);
			std::string line;
			while (std::getline(in, line))
			{
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (line.empty() || line[0] == '#') continue;
				std::vector<std::string> f = splitTabs(line);
				if (f.size() < 4) continue;
				Repo r;
				r.url = unescapeField(f[0]);
				if (!validRepoUrl(r.url)) continue;
				int64_t ts = 0;
				if (!toInt(f[3], ts)) continue;
				r.name = unescapeField(f[1]);
				r.etag = unescapeField(f[2]);
				r.lastFetched = ts;
				// Sources works offline: the last good index is kept beside the list and read back here
				std::string json;
				if (readWhole(stateDirForIndex(repoHash(r.url)), json))
				{
					RepoIndex idx;
					std::string err;
					if (parseRepoIndex(json, idx, err))
					{
						r.indexJson = std::move(json);
						r.index = std::move(idx);
						r.haveIndex = true;
						if (r.name.empty()) r.name = r.index.name;
					}
				}
				bool known = false;
				for (auto& o : repos_)
					if (o.url == r.url)
					{
						o = std::move(r);
						known = true;
						break;
					}
				if (!known) repos_.push_back(std::move(r));
			}
		}

		if (readWhole(sourcesDir() + "/installed.tsv", text))
		{
			std::istringstream in(text);
			std::string line;
			while (std::getline(in, line))
			{
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (line.empty() || line[0] == '#') continue;
				std::vector<std::string> f = splitTabs(line);
				if (f.size() < 7) continue;
				Installed e;
				e.repoHash = unescapeField(f[0]);
				e.id = unescapeField(f[1]);
				if (!isHex12(e.repoHash) || !validId(e.id)) continue;
				e.version = unescapeField(f[2]);
				e.name = unescapeField(f[3]);
				e.lang = unescapeField(f[4]);
				if (!toBool(f[5], e.adult)) continue;
				// The path in the file is where it was when saved; the state folder may be mounted elsewhere now,
				// so the canonical path under this stateDir is the one that counts.
				e.file = sourcesDir() + "/" + e.repoHash + "/" + e.id + ".lua";
				std::string fid = fullId(e.repoHash, e.id);
				bool known = false;
				for (auto& o : installed_)
					if (fullId(o.repoHash, o.id) == fid)
					{
						o = std::move(e);
						known = true;
						break;
					}
				if (!known) installed_.push_back(std::move(e));
			}
		}

		serial_++;
		return true;
	}

	bool MangaRepos::save()
	{
		if (stateDir_.empty()) return false;
		mkdirP(mangaDir());
		mkdirP(mangaDir() + "/repos");
		mkdirP(sourcesDir());

		std::string reposText = std::string(REPOS_HEADER) + "\n";
		char buf[64];
		for (const auto& r : repos_)
		{
			reposText += escapeField(r.url) + "\t" + escapeField(r.name) + "\t" + escapeField(r.etag) + "\t";
			snprintf(buf, sizeof buf, "%lld", (long long)r.lastFetched);
			reposText += buf;
			reposText += "\n";
		}
		bool ok = writeFileAtomic(mangaDir() + "/repos.tsv", reposText);

		// the indexes are separate files: a failed fetch never has to rewrite a repository list to keep the cache
		for (const auto& r : repos_)
			if (r.haveIndex) ok = writeFileAtomic(stateDirForIndex(repoHash(r.url)), r.indexJson) && ok;

		std::string sourcesText = std::string(SOURCES_HEADER) + "\n";
		for (const auto& e : installed_)
			sourcesText += escapeField(e.repoHash) + "\t" + escapeField(e.id) + "\t" + escapeField(e.version) + "\t" +
				escapeField(e.name) + "\t" + escapeField(e.lang) + "\t" + (e.adult ? "1" : "0") + "\t" +
				escapeField(e.file) + "\n";
		ok = writeFileAtomic(sourcesDir() + "/installed.tsv", sourcesText) && ok;
		return ok;
	}

	MangaRepos::Repo* MangaRepos::findRepo(const std::string& url)
	{
		for (auto& r : repos_)
			if (r.url == url) return &r;
		return nullptr;
	}

	const MangaRepos::Repo* MangaRepos::findRepo(const std::string& url) const
	{
		for (const auto& r : repos_)
			if (r.url == url) return &r;
		return nullptr;
	}

	MangaRepos::Installed* MangaRepos::findInstalled(const std::string& fullId)
	{
		for (auto& e : installed_)
			if (bp::MangaRepos::fullId(e.repoHash, e.id) == fullId) return &e;
		return nullptr;
	}

	const MangaRepos::Installed* MangaRepos::findInstalled(const std::string& fullId) const
	{
		for (const auto& e : installed_)
			if (bp::MangaRepos::fullId(e.repoHash, e.id) == fullId) return &e;
		return nullptr;
	}

	void MangaRepos::addEvent(Event::Kind kind, const std::string& repo, const std::string& source,
		const std::string& message)
	{
		Event e;
		e.kind = kind;
		e.repo = repo;
		e.source = source;
		e.message = message;
		events_.push_back(std::move(e));
	}

	std::vector<MangaRepos::Event> MangaRepos::takeEvents()
	{
		std::vector<Event> out;
		out.swap(events_);
		return out;
	}

	bool MangaRepos::addRepo(const std::string& url, std::string& error)
	{
		error.clear();
		if (!validRepoUrl(url))
		{
			error = "not a URL";
			return false;
		}
		if (findRepo(url))
		{
			error = "already added";
			return false;
		}
		Repo r;
		r.url = url;
		repos_.push_back(std::move(r));
		serial_++;
		startIndexFetch(repos_.back());
		return true;
	}

	void MangaRepos::cancelForRepo(const std::string& repoUrl)
	{
		for (auto it = pending_.begin(); it != pending_.end();)
		{
			if (it->second.repo == repoUrl)
			{
				fetch_.cancel(it->first);
				it = pending_.erase(it);
			}
			else
				++it;
		}
	}

	void MangaRepos::removeInstalled(const Installed& entry)
	{
		if (!entry.file.empty()) unlink(entry.file.c_str());
		std::string prefs = prefsDir() + "/" + entry.repoHash + "-" + entry.id + ".json";
		unlink(prefs.c_str());
		std::string fid = fullId(entry.repoHash, entry.id);
		installed_.erase(std::remove_if(installed_.begin(), installed_.end(),
							 [&](const Installed& e) { return fullId(e.repoHash, e.id) == fid; }),
			installed_.end());
	}

	bool MangaRepos::removeRepo(const std::string& url, bool alsoSources)
	{
		if (!findRepo(url)) return false;
		std::string hash = repoHash(url);
		cancelForRepo(url);
		if (alsoSources)
		{
			// gather first: uninstall erases from installed_, which a loop over it must not see
			std::vector<std::string> ids;
			for (const auto& e : installed_)
				if (e.repoHash == hash) ids.push_back(fullId(e.repoHash, e.id));
			for (const auto& id : ids) uninstall(id);
		}
		unlink(stateDirForIndex(hash).c_str());
		repos_.erase(std::remove_if(repos_.begin(), repos_.end(), [&](const Repo& r) { return r.url == url; }),
			repos_.end());
		serial_++;
		return true;
	}

	bool MangaRepos::hasIndexFetch(const std::string& url) const
	{
		for (const auto& kv : pending_)
			if (kv.second.kind == PendingKind::Index && kv.second.repo == url) return true;
		return false;
	}

	bool MangaRepos::startIndexFetch(const Repo& repo)
	{
		FetchApi::Headers headers;
		if (!repo.etag.empty()) headers.push_back({"If-None-Match", repo.etag});
		uint64_t id = fetch_.get(repo.url, headers, (int64_t)MAX_INDEX_BYTES);
		if (id == 0)
		{
			addEvent(Event::RepoFailed, repo.url, "", "the fetcher has stopped");
			return false;
		}
		Pending p;
		p.kind = PendingKind::Index;
		p.repo = repo.url;
		pending_[id] = std::move(p);
		return true;
	}

	void MangaRepos::refresh(bool force)
	{
		int64_t t = now();
		for (const auto& r : repos_)
		{
			if (hasIndexFetch(r.url)) continue;
			// A clock that went backwards is not proof that the hour passed: fetch rather than skip forever.
			if (!force && r.lastFetched != 0 && t >= r.lastFetched && t - r.lastFetched < REFRESH_SECONDS) continue;
			startIndexFetch(r);
		}
	}

	bool MangaRepos::startScriptFetch(const std::string& repoUrl, const std::string& sourceId, bool update)
	{
		std::string fid = fullId(repoHash(repoUrl), sourceId);
		Repo* r = findRepo(repoUrl);
		if (!r || !r->haveIndex)
		{
			addEvent(Event::InstallFailed, repoUrl, fid, "no repository index");
			return false;
		}
		const RepoSource* src = nullptr;
		for (const auto& s : r->index.sources)
			if (s.id == sourceId)
			{
				src = &s;
				break;
			}
		if (!src)
		{
			addEvent(Event::InstallFailed, repoUrl, fid, "not in the repository index");
			return false;
		}
		for (const auto& kv : pending_)
			if (kv.second.kind == PendingKind::Script && kv.second.repo == repoUrl &&
				kv.second.sourceId == sourceId)
			{
				addEvent(Event::InstallFailed, repoUrl, fid, "already downloading");
				return false;
			}
		uint64_t id = fetch_.get(repoResolve(r->url, src->url), {}, (int64_t)MAX_SCRIPT_BYTES);
		if (id == 0)
		{
			addEvent(Event::InstallFailed, repoUrl, fid, "the fetcher has stopped");
			return false;
		}
		Pending p;
		p.kind = PendingKind::Script;
		p.repo = repoUrl;
		p.sourceId = sourceId;
		p.update = update;
		pending_[id] = std::move(p);
		return true;
	}

	bool MangaRepos::install(const std::string& repoUrl, const std::string& sourceId)
	{
		std::string fid = fullId(repoHash(repoUrl), sourceId);
		if (!findRepo(repoUrl))
		{
			addEvent(Event::InstallFailed, repoUrl, fid, "repository not added");
			return false;
		}
		if (findInstalled(fid))
		{
			addEvent(Event::InstallFailed, repoUrl, fid, "already installed");
			return false;
		}
		return startScriptFetch(repoUrl, sourceId, false);
	}

	bool MangaRepos::update(const std::string& repoUrl, const std::string& sourceId)
	{
		std::string fid = fullId(repoHash(repoUrl), sourceId);
		const Repo* r = findRepo(repoUrl);
		if (!r)
		{
			addEvent(Event::InstallFailed, repoUrl, fid, "repository not added");
			return false;
		}
		const Installed* inst = findInstalled(fid);
		if (!inst)
		{
			addEvent(Event::InstallFailed, repoUrl, fid, "not installed");
			return false;
		}
		if (!r->haveIndex)
		{
			addEvent(Event::InstallFailed, repoUrl, fid, "no repository index");
			return false;
		}
		const RepoSource* src = nullptr;
		for (const auto& s : r->index.sources)
			if (s.id == sourceId)
			{
				src = &s;
				break;
			}
		if (!src)
		{
			addEvent(Event::InstallFailed, repoUrl, fid, "not in the repository index");
			return false;
		}
		if (!updateAvailable(inst->version, src->version))
		{
			addEvent(Event::InstallFailed, repoUrl, fid, "already up to date");
			return false;
		}
		return startScriptFetch(repoUrl, sourceId, true);
	}

	bool MangaRepos::uninstall(const std::string& fullId)
	{
		size_t slash = fullId.find('/');
		if (slash == std::string::npos) return false;
		std::string hash = fullId.substr(0, slash);
		std::string id = fullId.substr(slash + 1);
		if (!isHex12(hash) || !validId(id)) return false;
		Installed* inst = findInstalled(fullId);
		if (!inst) return false;
		// a download for it must not land after the user removed it
		for (auto it = pending_.begin(); it != pending_.end();)
		{
			if (it->second.kind == PendingKind::Script && it->second.sourceId == id &&
				repoHash(it->second.repo) == hash)
			{
				fetch_.cancel(it->first);
				it = pending_.erase(it);
			}
			else
				++it;
		}
		removeInstalled(*inst);
		serial_++;
		return true;
	}

	bool MangaRepos::onFetched(const MangaFetch::Result& result)
	{
		auto it = pending_.find(result.id);
		if (it == pending_.end()) return false;
		Pending p = it->second;
		pending_.erase(it);
		if (p.kind == PendingKind::Index) handleIndex(p, result);
		else handleScript(p, result);
		return true;
	}

	void MangaRepos::handleIndex(const Pending& p, const MangaFetch::Result& result)
	{
		Repo* r = findRepo(p.repo);
		if (!r) return;
		// An attempt counts, success or not: Sources opening again within the hour must not hammer a server that
		// just failed. A refresh (force) still goes through.
		r->lastFetched = now();
		if (result.status == 304)
		{
			if (!r->haveIndex)
			{
				addEvent(Event::RepoFailed, r->url, "", "not modified but no cached index");
				return;
			}
			serial_++;
			addEvent(Event::RepoLoaded, r->url, "", r->name.empty() ? r->url : r->name);
			return;
		}
		if (!result.error.empty() || result.status < 200 || result.status >= 300)
		{
			addEvent(Event::RepoFailed, r->url, "", overLimit(result.error) ? "index larger than 1 MB" : failureOf(result));
			return;
		}
		if (result.body.size() > MAX_INDEX_BYTES)
		{
			addEvent(Event::RepoFailed, r->url, "", "index larger than 1 MB");
			return;
		}
		std::string json(result.body.begin(), result.body.end());
		RepoIndex idx;
		std::string err;
		if (!parseRepoIndex(json, idx, err))
		{
			// the old index stays: a broken fetch must not empty the Sources page
			addEvent(Event::RepoFailed, r->url, "", err);
			return;
		}
		r->indexJson = std::move(json);
		r->index = std::move(idx);
		r->haveIndex = true;
		if (!r->index.name.empty()) r->name = r->index.name;
		r->etag = indexEtag(r->indexJson);
		serial_++;
		addEvent(Event::RepoLoaded, r->url, "", r->name.empty() ? r->url : r->name);
	}

	void MangaRepos::handleScript(const Pending& p, const MangaFetch::Result& result)
	{
		std::string hash = repoHash(p.repo);
		std::string fid = fullId(hash, p.sourceId);
		Repo* r = findRepo(p.repo);
		if (!r || !r->haveIndex)
		{
			addEvent(Event::InstallFailed, p.repo, fid, "no repository index");
			return;
		}
		const RepoSource* src = nullptr;
		for (const auto& s : r->index.sources)
			if (s.id == p.sourceId)
			{
				src = &s;
				break;
			}
		if (!src)
		{
			addEvent(Event::InstallFailed, p.repo, fid, "not in the repository index");
			return;
		}
		Installed* existing = findInstalled(fid);
		if (p.update && !existing)
		{
			addEvent(Event::InstallFailed, p.repo, fid, "not installed any more");
			return;
		}
		if (!p.update && existing)
		{
			addEvent(Event::InstallFailed, p.repo, fid, "already installed");
			return;
		}
		if (result.body.size() > MAX_SCRIPT_BYTES || overLimit(result.error))
		{
			addEvent(Event::InstallFailed, p.repo, fid, "script larger than 256 KB");
			return;
		}
		if (!result.error.empty())
		{
			addEvent(Event::InstallFailed, p.repo, fid, result.error);
			return;
		}
		if (result.status != 200)
		{
			addEvent(Event::InstallFailed, p.repo, fid, failureOf(result));
			return;
		}
		if (!sha256Matches(src->sha256, result.body.data(), result.body.size()))
		{
			addEvent(Event::InstallFailed, p.repo, fid, "SHA-256 doesn't match the index");
			return;
		}
		std::string text(result.body.begin(), result.body.end());
		if (!validator_)
		{
			addEvent(Event::InstallFailed, p.repo, fid, "no script check is configured");
			return;
		}
		std::string why;
		if (!validator_(text, src->id, why))
		{
			addEvent(Event::InstallFailed, p.repo, fid, why.empty() ? "the script was refused" : why);
			return;
		}
		std::string dir = sourcesDir() + "/" + hash;
		if (!mkdirP(dir))
		{
			addEvent(Event::InstallFailed, p.repo, fid, "couldn't make " + dir);
			return;
		}
		std::string path = dir + "/" + src->id + ".lua";
		if (!writeFileAtomic(path, text))
		{
			addEvent(Event::InstallFailed, p.repo, fid, "couldn't write " + path);
			return;
		}
		if (existing)
		{
			existing->version = src->version;
			existing->name = src->name;
			existing->lang = src->lang;
			existing->adult = src->adult;
			existing->file = path;
		}
		else
		{
			Installed e;
			e.repoHash = hash;
			e.id = src->id;
			e.version = src->version;
			e.name = src->name;
			e.lang = src->lang;
			e.adult = src->adult;
			e.file = path;
			installed_.push_back(std::move(e));
		}
		serial_++;
		addEvent(p.update ? Event::Updated : Event::Installed, p.repo, fid, src->version);
	}

	std::vector<MangaRepos::AvailableSource> MangaRepos::available() const
	{
		std::vector<AvailableSource> out;
		for (const auto& r : repos_)
		{
			if (!r.haveIndex) continue;
			std::string hash = repoHash(r.url);
			for (const auto& s : r.index.sources)
			{
				if (findInstalled(fullId(hash, s.id))) continue;
				AvailableSource a;
				a.repo = r.url;
				a.repoName = r.name.empty() ? r.url : r.name;
				a.repoHash = hash;
				a.id = s.id;
				a.fullId = fullId(hash, s.id);
				a.name = s.name;
				a.version = s.version;
				a.lang = s.lang;
				a.icon = s.icon;
				a.adult = s.adult;
				out.push_back(std::move(a));
			}
		}
		return out;
	}

	std::vector<MangaRepos::InstalledSource> MangaRepos::installed() const
	{
		std::vector<InstalledSource> out;
		for (const auto& e : installed_)
		{
			InstalledSource s;
			s.repoHash = e.repoHash;
			s.id = e.id;
			s.fullId = fullId(e.repoHash, e.id);
			s.version = e.version;
			s.name = e.name;
			s.lang = e.lang;
			s.file = e.file;
			s.adult = e.adult;
			for (const auto& r : repos_)
				if (repoHash(r.url) == e.repoHash)
				{
					s.repo = r.url;
					if (r.haveIndex)
						for (const auto& src : r.index.sources)
							if (src.id == e.id)
							{
								s.updateAvailable = updateAvailable(e.version, src.version);
								break;
							}
					break;
				}
			out.push_back(std::move(s));
		}
		return out;
	}

	std::vector<MangaRepos::RepoInfo> MangaRepos::repos() const
	{
		std::vector<RepoInfo> out;
		for (const auto& r : repos_)
		{
			RepoInfo i;
			i.url = r.url;
			i.name = r.name;
			i.etag = r.etag;
			i.lastFetched = r.lastFetched;
			i.haveIndex = r.haveIndex;
			i.sourceCount = r.index.sources.size();
			out.push_back(std::move(i));
		}
		return out;
	}
}
