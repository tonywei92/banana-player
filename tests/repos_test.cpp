// The repository manager (docs/design/manga.md 5.3) with a fake fetcher: no thread, no socket, no Lua host. The
// validator is the test's own, standing in for the sandbox the Lua library will provide.
#include "mangarepos.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace bp;

static int failures = 0;
#define CHECK_EQ(a, b) \
	do { auto va = (a); auto vb = (b); \
		if (!(va == vb)) { failures++; fprintf(stderr, "%s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); } } while (0)

namespace
{
	std::string sha(const std::string& s)
	{
		return sha256Hex((const uint8_t*)s.data(), s.size());
	}

	std::string script(const std::string& id, const std::string& version, bool bad = false)
	{
		return "return { api = 1, id = \"" + id + "\", name = \"" + id + "\", version = \"" + version +
			"\", lang = \"en\", nsfw = false }" + (bad ? " -- BAD" : "") + "\n";
	}

	struct SrcDef
	{
		std::string id, name, lang, version, file, body, shaOverride;
		bool adult = false;
	};

	std::string makeIndex(const std::string& name, const std::vector<SrcDef>& defs)
	{
		std::string j = "{\"format\":1,\"name\":\"" + name + "\",\"sources\":[";
		for (size_t i = 0; i < defs.size(); i++)
		{
			const SrcDef& d = defs[i];
			if (i) j += ",";
			j += "{\"id\":\"" + d.id + "\",\"name\":\"" + d.name + "\",\"lang\":\"" + d.lang +
				"\",\"version\":\"" + d.version + "\",\"api\":1,\"nsfw\":" + (d.adult ? "true" : "false") +
				",\"file\":\"" + d.file + "\",\"sha256\":\"" + (d.shaOverride.empty() ? sha(d.body) : d.shaOverride) +
				"\",\"size\":" + std::to_string(d.body.size()) + "}";
		}
		j += "]}";
		return j;
	}

	// A fetcher in the shape MangaFetch answers in, without its thread: get() queues, answer() makes the result.
	class FakeFetch : public FetchApi
	{
	public:
		struct Req
		{
			uint64_t id = 0;
			std::string url;
			Headers headers;
			int64_t maxBytes = 0;
		};

		std::vector<Req> reqs;
		std::vector<uint64_t> cancelled;
		std::map<std::string, std::string> bodies;	// url -> the bytes a 200 carries
		std::set<std::string> fail;					// urls whose request fails (no signal, offline)

		uint64_t get(const std::string& url, const Headers& headers, int64_t maxBytes, const std::string& toFile) override
		{
			(void)toFile;
			return get3(url, headers, maxBytes);
		}
		uint64_t get3(const std::string& url, const Headers& headers, int64_t maxBytes)
		{
			Req q;
			q.id = next_++;
			q.url = url;
			q.headers = headers;
			q.maxBytes = maxBytes;
			reqs.push_back(std::move(q));
			return reqs.back().id;
		}

		void cancel(uint64_t id) override { cancelled.push_back(id); }

		MangaFetch::Result answer(const Req& q) const
		{
			MangaFetch::Result r;
			r.id = q.id;
			auto b = bodies.find(q.url);
			if (fail.count(q.url) || b == bodies.end())
			{
				r.status = 0;
				r.error = "couldn't connect";
				return r;
			}
			for (const auto& h : q.headers)
				if (h.first == "If-None-Match" && h.second == "\"" + sha(b->second) + "\"")
				{
					// the real fetcher reports a 304 as status 304 *and* an "HTTP 304" error: the manager has
					// to read the status first, so the fake says the same
					r.status = 304;
					r.error = "HTTP 304";
					return r;
				}
			r.status = 200;
			r.body.assign(b->second.begin(), b->second.end());
			return r;
		}

	private:
		uint64_t next_ = 1;
	};

	// Serves everything queued so far, in the order main's loop would.
	int pump(MangaRepos& mgr, FakeFetch& fetch)
	{
		std::vector<FakeFetch::Req> qs;
		qs.swap(fetch.reqs);
		int handled = 0;
		for (const auto& q : qs)
		{
			MangaFetch::Result r = fetch.answer(q);
			if (mgr.onFetched(r)) handled++;
		}
		return handled;
	}

	bool exists(const std::string& path)
	{
		struct stat st;
		return stat(path.c_str(), &st) == 0;
	}

	bool writeFile(const std::string& path, const std::string& text)
	{
		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f) return false;
		f.write(text.data(), (std::streamsize)text.size());
		return bool(f);
	}

	std::string readAll(const std::string& path)
	{
		std::ifstream f(path, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	}

	bool hasAvailable(const MangaRepos& m, const std::string& id)
	{
		for (const auto& a : m.available())
			if (a.id == id) return true;
		return false;
	}

	bool installed(const MangaRepos& m, const std::string& fullId, MangaRepos::InstalledSource& out)
	{
		for (const auto& i : m.installed())
			if (i.fullId == fullId)
			{
				out = i;
				return true;
			}
		return false;
	}

	// The sandbox's stand-in: a returned table naming this id, and none of the marker text the test uses to make a
	// script invalid. A real validator also checks api, the required functions and the fresh state (design 5.3/5.4).
	bool validate(const std::string& text, const std::string& expectedId, std::string& error)
	{
		if (text.find("BAD") != std::string::npos)
		{
			error = "the script is not a source";
			return false;
		}
		if (text.find("id = \"" + expectedId + "\"") == std::string::npos)
		{
			error = "the script's id doesn't match the index";
			return false;
		}
		return true;
	}
}

static void testRepos()
{
	char tmpl[] = "/tmp/bp-repos-XXXXXX";
	char* made = mkdtemp(tmpl);
	CHECK_EQ(made != nullptr, true);
	if (!made) return;
	const std::string D = made;

	FakeFetch fetch;
	int64_t clock = 1000000;
	MangaRepos mgr(fetch, validate);
	mgr.setClock([&] { return clock; });
	CHECK_EQ(mgr.load(D), true);

	// ---- URLs and duplicates ----
	std::string err;
	CHECK_EQ(mgr.addRepo("no scheme at all", err), false);
	CHECK_EQ(err.empty(), false);
	CHECK_EQ(mgr.addRepo("ftp://example.org/repo.json", err), false);
	CHECK_EQ(mgr.addRepo("https://user:secret@example.org/repo.json", err), false);	// credentials, never
	CHECK_EQ(mgr.addRepo("https:///nohost.json", err), false);
	CHECK_EQ(mgr.repos().size(), size_t(0));

	// ---- a repository: its index is fetched and checked, its sources listed ----
	const std::string repoUrl = "https://example.org/repo/index.json";
	const std::string base = "https://example.org/repo/";
	const std::string hash = MangaRepos::repoHash(repoUrl);
	CHECK_EQ(hash.size(), size_t(12));
	CHECK_EQ(hash, sha(repoUrl).substr(0, 12));

	std::vector<SrcDef> defs;
	defs.push_back({"one", "Example One", "en", "1.0.0", "sources/one.lua", script("one", "1.0.0"), "", false});
	defs.push_back({"two", "Example Two", "all", "1.0.0", "sources/two.lua", script("two", "1.0.0"), "", true});
	defs.push_back({"wrong", "Wrong Sha", "en", "1.0.0", "sources/wrong.lua", script("wrong", "1.0.0"),
		std::string(64, '0'), false});
	defs.push_back({"big", "Too Big", "en", "1.0.0", "sources/big.lua", std::string(300000, 'x'), "", false});
	defs.push_back({"refused", "Refused", "en", "1.0.0", "sources/refused.lua", script("refused", "1.0.0", true), "",
		false});
	std::string indexJson = makeIndex("Example repository", defs);
	fetch.bodies[repoUrl] = indexJson;
	for (const auto& d : defs) fetch.bodies[base + d.file] = d.body;

	CHECK_EQ(mgr.addRepo(repoUrl, err), true);
	CHECK_EQ(mgr.addRepo(repoUrl, err), false);	// no duplicates
	CHECK_EQ(err, std::string("already added"));
	CHECK_EQ(fetch.reqs.size(), size_t(1));
	CHECK_EQ(fetch.reqs[0].url, repoUrl);
	CHECK_EQ(fetch.reqs[0].maxBytes, (int64_t)MangaRepos::MAX_INDEX_BYTES);
	CHECK_EQ(pump(mgr, fetch), 1);
	std::vector<MangaRepos::Event> ev = mgr.takeEvents();
	CHECK_EQ(ev.size(), size_t(1));
	if (!ev.empty()) CHECK_EQ(ev[0].kind, MangaRepos::Event::RepoLoaded);
	CHECK_EQ(mgr.repos().size(), size_t(1));
	if (!mgr.repos().empty())
	{
		CHECK_EQ(mgr.repos()[0].name, std::string("Example repository"));
		CHECK_EQ(mgr.repos()[0].haveIndex, true);
		CHECK_EQ(mgr.repos()[0].sourceCount, size_t(5));
	}
	CHECK_EQ(mgr.available().size(), size_t(5));
	CHECK_EQ(hasAvailable(mgr, "one"), true);
	CHECK_EQ(hasAvailable(mgr, "two"), true);
	CHECK_EQ(hasAvailable(mgr, "wrong"), true);	// listed; the checksum is weighed at install
	bool adultSeen = false, plainSeen = false;
	for (const auto& a : mgr.available())
	{
		CHECK_EQ(a.repo, repoUrl);
		CHECK_EQ(a.repoName, std::string("Example repository"));
		CHECK_EQ(a.fullId, hash + "/" + a.id);
		if (a.id == "two") { adultSeen = a.adult; CHECK_EQ(a.lang, std::string("all")); }
		if (a.id == "one") { plainSeen = !a.adult; CHECK_EQ(a.lang, std::string("en")); }
	}
	CHECK_EQ(adultSeen, true);
	CHECK_EQ(plainSeen, true);

	// ---- at most once an hour; If-None-Match; a 304 keeps the cached index ----
	clock += 100;
	mgr.refresh(false);
	CHECK_EQ(fetch.reqs.size(), size_t(0));
	clock += MangaRepos::REFRESH_SECONDS;
	mgr.refresh(false);
	CHECK_EQ(fetch.reqs.size(), size_t(1));
	if (!fetch.reqs.empty())
	{
		CHECK_EQ(fetch.reqs[0].headers.size(), size_t(1));
		if (!fetch.reqs[0].headers.empty())
		{
			CHECK_EQ(fetch.reqs[0].headers[0].first, std::string("If-None-Match"));
			CHECK_EQ(fetch.reqs[0].headers[0].second, "\"" + sha(indexJson) + "\"");
		}
	}
	CHECK_EQ(pump(mgr, fetch), 1);	// the fake answers 304
	ev = mgr.takeEvents();
	CHECK_EQ(ev.size(), size_t(1));
	if (!ev.empty()) CHECK_EQ(ev[0].kind, MangaRepos::Event::RepoLoaded);
	CHECK_EQ(mgr.repos()[0].haveIndex, true);
	CHECK_EQ(mgr.available().size(), size_t(5));

	// Refresh (force) ignores the hour
	clock += 10;
	mgr.refresh(true);
	CHECK_EQ(fetch.reqs.size(), size_t(1));
	pump(mgr, fetch);
	mgr.takeEvents();

	// ---- install: the script lands in sources/<repo-hash>/<id>.lua and is recorded ----
	CHECK_EQ(mgr.install(repoUrl, "one"), true);
	CHECK_EQ(fetch.reqs.size(), size_t(1));
	CHECK_EQ(fetch.reqs[0].url, base + "sources/one.lua");
	CHECK_EQ(fetch.reqs[0].maxBytes, (int64_t)MangaRepos::MAX_SCRIPT_BYTES);
	CHECK_EQ(pump(mgr, fetch), 1);
	ev = mgr.takeEvents();
	CHECK_EQ(ev.size(), size_t(1));
	if (!ev.empty())
	{
		CHECK_EQ(ev[0].kind, MangaRepos::Event::Installed);
		CHECK_EQ(ev[0].source, hash + "/one");
		CHECK_EQ(ev[0].message, std::string("1.0.0"));
	}
	const std::string onePath = D + "/manga/sources/" + hash + "/one.lua";
	CHECK_EQ(exists(onePath), true);
	CHECK_EQ(readAll(onePath), defs[0].body);
	CHECK_EQ(mgr.installed().size(), size_t(1));
	MangaRepos::InstalledSource info;
	if (installed(mgr, hash + "/one", info))
	{
		CHECK_EQ(info.version, std::string("1.0.0"));
		CHECK_EQ(info.name, std::string("Example One"));
		CHECK_EQ(info.lang, std::string("en"));
		CHECK_EQ(info.adult, false);
		CHECK_EQ(info.updateAvailable, false);
		CHECK_EQ(info.file, onePath);
	}
	CHECK_EQ(hasAvailable(mgr, "one"), false);
	CHECK_EQ(mgr.available().size(), size_t(4));

	// a second install of the same id is refused before any fetch
	CHECK_EQ(mgr.install(repoUrl, "one"), false);
	pump(mgr, fetch);
	ev = mgr.takeEvents();
	CHECK_EQ(ev.size(), size_t(1));
	if (!ev.empty()) CHECK_EQ(ev[0].kind, MangaRepos::Event::InstallFailed);

	// ---- a wrong SHA-256: refused, nothing written ----
	CHECK_EQ(mgr.install(repoUrl, "wrong"), true);
	pump(mgr, fetch);
	ev = mgr.takeEvents();
	CHECK_EQ(ev.size(), size_t(1));
	if (!ev.empty())
	{
		CHECK_EQ(ev[0].kind, MangaRepos::Event::InstallFailed);
		CHECK_EQ(ev[0].message.find("SHA-256") != std::string::npos, true);
	}
	CHECK_EQ(exists(D + "/manga/sources/" + hash + "/wrong.lua"), false);
	CHECK_EQ(mgr.installed().size(), size_t(1));

	// ---- an oversized script: refused, nothing written ----
	CHECK_EQ(mgr.install(repoUrl, "big"), true);
	pump(mgr, fetch);
	ev = mgr.takeEvents();
	CHECK_EQ(ev.size(), size_t(1));
	if (!ev.empty())
	{
		CHECK_EQ(ev[0].kind, MangaRepos::Event::InstallFailed);
		CHECK_EQ(ev[0].message.find("256 KB") != std::string::npos, true);
	}
	CHECK_EQ(exists(D + "/manga/sources/" + hash + "/big.lua"), false);
	CHECK_EQ(mgr.installed().size(), size_t(1));

	// ---- the validator refuses: its reason is reported, nothing written ----
	CHECK_EQ(mgr.install(repoUrl, "refused"), true);
	pump(mgr, fetch);
	ev = mgr.takeEvents();
	CHECK_EQ(ev.size(), size_t(1));
	if (!ev.empty())
	{
		CHECK_EQ(ev[0].kind, MangaRepos::Event::InstallFailed);
		CHECK_EQ(ev[0].message, std::string("the script is not a source"));
	}
	CHECK_EQ(exists(D + "/manga/sources/" + hash + "/refused.lua"), false);
	CHECK_EQ(mgr.installed().size(), size_t(1));

	// ---- save and load: the list, the record and the index come back ----
	CHECK_EQ(mgr.save(), true);
	CHECK_EQ(exists(D + "/manga/repos.tsv"), true);
	CHECK_EQ(exists(D + "/manga/repos/" + hash + ".json"), true);
	std::string sourcesTsv = readAll(D + "/manga/sources/installed.tsv");
	CHECK_EQ(sourcesTsv.find(hash + "\tone\t1.0.0\tExample One\ten\t0\t") != std::string::npos, true);
	std::string reposTsv = readAll(D + "/manga/repos.tsv");
	CHECK_EQ(reposTsv.find(repoUrl + "\tExample repository\t") != std::string::npos, true);
	{
		MangaRepos again(fetch, validate);
		again.setClock([&] { return clock; });
		CHECK_EQ(again.load(D), true);
		CHECK_EQ(again.repos().size(), size_t(1));
		CHECK_EQ(again.repos()[0].name, std::string("Example repository"));
		CHECK_EQ(again.repos()[0].haveIndex, true);
		CHECK_EQ(again.repos()[0].sourceCount, size_t(5));
		CHECK_EQ(again.installed().size(), size_t(1));
		MangaRepos::InstalledSource back;
		CHECK_EQ(installed(again, hash + "/one", back), true);
		CHECK_EQ(again.available().size(), size_t(4));
	}

	// ---- update: offered only when the index rises, then installed; prefs kept ----
	CHECK_EQ(mkdir((D + "/manga/prefs").c_str(), 0755), 0);
	const std::string prefsPath = D + "/manga/prefs/" + hash + "-one.json";
	CHECK_EQ(writeFile(prefsPath, "{\"quality\":\"High\"}"), true);

	std::vector<SrcDef> defs2 = defs;
	defs2[0].version = "1.2.0";
	defs2[0].body = script("one", "1.2.0") + "-- v2\n";
	fetch.bodies[repoUrl] = makeIndex("Example repository", defs2);
	fetch.bodies[base + defs2[0].file] = defs2[0].body;
	clock += MangaRepos::REFRESH_SECONDS + 1;
	mgr.refresh(false);
	pump(mgr, fetch);
	mgr.takeEvents();
	if (installed(mgr, hash + "/one", info))
		CHECK_EQ(info.updateAvailable, true);
	CHECK_EQ(mgr.update(repoUrl, "one"), true);
	pump(mgr, fetch);
	ev = mgr.takeEvents();
	CHECK_EQ(ev.size(), size_t(1));
	if (!ev.empty())
	{
		CHECK_EQ(ev[0].kind, MangaRepos::Event::Updated);
		CHECK_EQ(ev[0].message, std::string("1.2.0"));
	}
	if (installed(mgr, hash + "/one", info))
	{
		CHECK_EQ(info.version, std::string("1.2.0"));
		CHECK_EQ(info.updateAvailable, false);
	}
	CHECK_EQ(readAll(onePath), defs2[0].body);
	CHECK_EQ(exists(prefsPath), true);
	CHECK_EQ(readAll(prefsPath), std::string("{\"quality\":\"High\"}"));

	// the same version is not offered again
	CHECK_EQ(mgr.update(repoUrl, "one"), false);
	pump(mgr, fetch);
	ev = mgr.takeEvents();
	CHECK_EQ(ev.size(), size_t(1));
	if (!ev.empty()) CHECK_EQ(ev[0].kind, MangaRepos::Event::InstallFailed);

	// ---- offline: a failed refresh keeps and uses the last good index ----
	const std::string repoC = "https://cache.example/index.json";
	SrcDef solo{"solo", "Solo", "en", "1.0.0", "solo.lua", script("solo", "1.0.0"), "", false};
	std::string indexC = makeIndex("Cached repository", {solo});
	fetch.bodies[repoC] = indexC;
	fetch.bodies["https://cache.example/solo.lua"] = solo.body;
	CHECK_EQ(mgr.addRepo(repoC, err), true);
	pump(mgr, fetch);
	mgr.takeEvents();
	CHECK_EQ(hasAvailable(mgr, "solo"), true);
	fetch.fail.insert(repoC);
	clock += MangaRepos::REFRESH_SECONDS + 1;
	mgr.refresh(false);
	pump(mgr, fetch);
	ev = mgr.takeEvents();
	bool cFailed = false;
	for (const auto& e : ev)
		if (e.kind == MangaRepos::Event::RepoFailed && e.repo == repoC) cFailed = true;
	CHECK_EQ(cFailed, true);
	CHECK_EQ(hasAvailable(mgr, "solo"), true);	// still there, from the cache
	CHECK_EQ(mgr.removeRepo(repoC, true), true);
	CHECK_EQ(hasAvailable(mgr, "solo"), false);

	// ---- removing a repository with its sources ----
	const std::string repoB = "https://bee.example/repo/index.json";
	const std::string baseB = "https://bee.example/repo/";
	const std::string hashB = MangaRepos::repoHash(repoB);
	SrcDef bee{"bee", "Bee", "en", "1.0.0", "bee.lua", script("bee", "1.0.0"), "", false};
	fetch.bodies[repoB] = makeIndex("Bee repository", {bee});
	fetch.bodies[baseB + "bee.lua"] = bee.body;
	CHECK_EQ(mgr.addRepo(repoB, err), true);
	pump(mgr, fetch);
	mgr.takeEvents();
	CHECK_EQ(mgr.install(repoB, "bee"), true);
	pump(mgr, fetch);
	mgr.takeEvents();
	const std::string beePath = D + "/manga/sources/" + hashB + "/bee.lua";
	const std::string beePrefs = D + "/manga/prefs/" + hashB + "-bee.json";
	CHECK_EQ(exists(beePath), true);
	CHECK_EQ(writeFile(beePrefs, "{}"), true);
	CHECK_EQ(mgr.removeRepo(repoB, true), true);
	CHECK_EQ(exists(beePath), false);
	CHECK_EQ(exists(beePrefs), false);
	MangaRepos::InstalledSource beeInfo;
	CHECK_EQ(installed(mgr, hashB + "/bee", beeInfo), false);

	// ---- removing a repository without its sources: the source stays installed ----
	CHECK_EQ(mgr.removeRepo(repoUrl, false), true);
	CHECK_EQ(exists(D + "/manga/repos/" + hash + ".json"), false);
	CHECK_EQ(exists(onePath), true);
	CHECK_EQ(installed(mgr, hash + "/one", info), true);
	CHECK_EQ(mgr.available().size(), size_t(0));
	if (installed(mgr, hash + "/one", info)) CHECK_EQ(info.repo, std::string(""));	// the repository is gone

	// ---- uninstall: script and prefs go ----
	CHECK_EQ(exists(prefsPath), true);
	CHECK_EQ(mgr.uninstall(hash + "/one"), true);
	CHECK_EQ(exists(onePath), false);
	CHECK_EQ(exists(prefsPath), false);
	CHECK_EQ(mgr.installed().size(), size_t(0));
	CHECK_EQ(mgr.uninstall(hash + "/one"), false);

	// no request outlived what asked for it
	CHECK_EQ(fetch.cancelled.empty(), true);

	// the folders are gone again (what the test created, nothing of the user's)
	unlink(onePath.c_str());
	unlink(prefsPath.c_str());
	unlink((D + "/manga/prefs/" + hashB + "-bee.json").c_str());
	unlink((D + "/manga/sources/installed.tsv").c_str());
	rmdir((D + "/manga/sources/" + hash).c_str());
	rmdir((D + "/manga/sources/" + hashB).c_str());
	rmdir((D + "/manga/sources").c_str());
	rmdir((D + "/manga/repos").c_str());
	unlink((D + "/manga/repos.tsv").c_str());
	rmdir((D + "/manga/prefs").c_str());
	rmdir((D + "/manga").c_str());
	rmdir(D.c_str());
}

int main()
{
	testRepos();
	if (failures)
	{
		fprintf(stderr, "repos: %d failure(s)\n", failures);
		return 1;
	}
	printf("repos: all passed\n");
	return 0;
}
