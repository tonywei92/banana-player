// The manga worker (docs/design/manga.md 7): one thread, serials, a waiting job replaced by a newer one for the same
// source and kind, cancelAll, a source's error and the idle close. The fixture scripts need no network; the ones that
// must fail ask for addresses 5.4 refuses (127.0.0.1:9, localhost, the LAN, [::1], the metadata address).
#include "mangaworker.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace bp;

static int failures = 0;
#define CHECK_EQ(a, b) \
	do { auto va = (a); auto vb = (b); \
		if (!(va == vb)) { failures++; fprintf(stderr, "%s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); } } while (0)
#define CHECK_TRUE(c) \
	do { if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

namespace
{
	using Clock = std::chrono::steady_clock;

	std::string tempDir(const std::string& name)
	{
		const char* dir = std::getenv("TMPDIR");
		std::string d = dir && *dir ? dir : "/tmp";
		if (!d.empty() && d.back() == '/') d.pop_back();
		return d + "/bpmanga-worker-" + std::to_string((long)getpid()) + "-" + name;
	}

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

	bool writeFile(const std::string& path, const std::string& text)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		out.write(text.data(), (std::streamsize)text.size());
		return !out.bad();
	}

	bool readFile(const std::string& path, std::string& out)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return false;
		out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		return !in.bad();
	}

	long msSince(Clock::time_point start)
	{
		return (long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
	}

	// The answers finished so far, waiting up to timeoutMs for `want` of them.
	std::vector<MangaWorker::Result> waitFor(MangaWorker& w, size_t want, int timeoutMs)
	{
		std::vector<MangaWorker::Result> out;
		auto start = Clock::now();
		while (out.size() < want)
		{
			auto batch = w.take();
			for (auto& r : batch) out.push_back(std::move(r));
			if (out.size() >= want) break;
			if (msSince(start) > timeoutMs) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return out;
	}

	// Everything that arrives in the next ms; the cancel tests use it to prove that nothing does.
	std::vector<MangaWorker::Result> drainFor(MangaWorker& w, int ms)
	{
		std::vector<MangaWorker::Result> out;
		auto start = Clock::now();
		while (true)
		{
			auto batch = w.take();
			for (auto& r : batch) out.push_back(std::move(r));
			if (msSince(start) >= ms) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return out;
	}

	bool hasSerial(const std::vector<MangaWorker::Result>& rs, uint64_t serial)
	{
		for (const auto& r : rs)
			if (r.serial == serial) return true;
		return false;
	}

	const MangaWorker::Result* bySerial(const std::vector<MangaWorker::Result>& rs, uint64_t serial)
	{
		for (const auto& r : rs)
			if (r.serial == serial) return &r;
		return nullptr;
	}

	MangaWorker::Job job(MangaWorker::Job::Kind kind, const std::string& sourceId, const std::string& scriptPath,
		int page = 1)
	{
		MangaWorker::Job j;
		j.kind = kind;
		j.sourceId = sourceId;
		j.scriptPath = scriptPath;
		j.page = page;
		return j;
	}

	// A source that answers every call from canned data: no network, so the worker's own behaviour is what is tested.
	const char* kCanned = R"LUA(
local base = "https://example.org"

return {
  api = 1,
  id = "canned",
  name = "Canned",
  lang = "en",
  version = "1.0.0",

  popular = function(page)
    bp.prefs.set("seen", tostring(page))
    local items = {}
    for i = 1, 2 do
      items[#items + 1] = { key = "/m/" .. i, title = "Title " .. i .. " p" .. page,
        cover = base .. "/c" .. i .. ".jpg", extra = "keep" }
    end
    return { items = items, more = page < 2 }
  end,

  latest = function(page)
    return { items = { { key = "/l/" .. page, title = "Latest p" .. page, cover = "" } }, more = false }
  end,

  search = function(query, page, filters)
    return { items = { { key = "/s/" .. query,
      title = query .. "/" .. page .. "/" .. tostring(filters.status or "none"), cover = "" } }, more = false }
  end,

  filters = function()
    return {
      { id = "status", type = "select", name = "Status",
        options = { { id = "any", name = "Any" }, { id = "ongoing", name = "Ongoing" } } },
    }
  end,

  details = function(manga)
    return { title = "Details " .. tostring(manga.key), author = "An Author", artist = "An Artist",
      description = "A description", genres = { "Action", "Drama" }, status = "ongoing",
      cover = base .. "/cover.jpg" }
  end,

  chapters = function(manga)
    return {
      { key = "/c/2", name = "Chapter 2", number = 2, date = 1731242096, group = "Scan", token = "tok2" },
      { key = "/c/1", name = "Chapter 1", number = 1 },
    }
  end,

  pages = function(chapter)
    return { { url = base .. "/pages/" .. tostring(chapter.token or "1") .. ".jpg" }, { key = "p2" } }
  end,

  image = function(page)
    return { url = base .. "/img/" .. tostring(page.key) .. ".jpg",
      headers = { ["X-Page"] = tostring(page.key) } }
  end,
}
)LUA";

	// Held on purpose: the replacement test needs a job still running while two more are queued behind it.
	const char* kSlow = R"LUA(
return {
  api = 1,
  id = "slow",
  name = "Slow",
  lang = "en",
  version = "1.0.0",

  popular = function(page)
    local x = 0
    for i = 1, 20000000 do x = x + i end
    return { items = { { key = "/m/" .. page, title = "Slow p" .. page, cover = "" } }, more = false }
  end,

  latest = function(page)
    local x = 0
    for i = 1, 20000000 do x = x + i end
    return { items = { { key = "/l/" .. page, title = "Slow latest p" .. page, cover = "" } }, more = false }
  end,

  details = function(manga) return { title = "Slow details" } end,
  chapters = function(manga) return {} end,
  pages = function(chapter) return {} end,
}
)LUA";

	const char* kErroring = R"LUA(
return {
  api = 1,
  id = "erroring",
  name = "Erroring",
  lang = "en",
  version = "1.0.0",
  popular = function(page) error("the site said no") end,
  details = function(manga) return {} end,
  chapters = function(manga) return {} end,
  pages = function(chapter) return {} end,
}
)LUA";

	// The one request that must fail: 5.4 refuses loopback before curl opens a socket, so no listener is needed.
	const char* kOffline = R"LUA(
return {
  api = 1,
  id = "offline",
  name = "Offline",
  lang = "en",
  version = "1.0.0",
  popular = function(page)
    bp.http.get("http://127.0.0.1:9/", { timeout = 3 })
    return { items = {}, more = false }
  end,
  details = function(manga) return {} end,
  chapters = function(manga) return {} end,
  pages = function(chapter) return {} end,
}
)LUA";

	// The card itself and the LAN, by name and by literal: all four are refused before any socket is opened.
	const char* kBlocked = R"LUA(
local urls = { "http://localhost/", "http://10.0.0.1/", "http://[::1]/", "http://169.254.169.254/" }
return {
  api = 1,
  id = "blocked",
  name = "Blocked",
  lang = "en",
  version = "1.0.0",
  popular = function(page)
    for _, u in ipairs(urls) do
      local ok, err = pcall(function() bp.http.get(u, { timeout = 3 }) end)
      if ok then error("not blocked: " .. u) end
      if not string.find(tostring(err), "Blocked") then error("wrong refusal for " .. u .. ": " .. tostring(err)) end
    end
    return { items = {}, more = false }
  end,
  details = function(manga) return {} end,
  chapters = function(manga) return {} end,
  pages = function(chapter) return {} end,
}
)LUA";

	// 5.4's 40 per call: the first 40 are caught, the 41st is the one the call must refuse.
	const char* kTooMany = R"LUA(
return {
  api = 1,
  id = "toomany",
  name = "Too many",
  lang = "en",
  version = "1.0.0",
  popular = function(page)
    for i = 1, 40 do
      pcall(function() bp.http.get("http://127.0.0.1:9/", { timeout = 3 }) end)
    end
    bp.http.get("http://127.0.0.1:9/", { timeout = 3 })
    return { items = {}, more = false }
  end,
  details = function(manga) return {} end,
  chapters = function(manga) return {} end,
  pages = function(chapter) return {} end,
}
)LUA";
}

int main()
{
	std::string root = tempDir("root");
	if (!mkdirP(root))
	{
		fprintf(stderr, "can't make %s\n", root.c_str());
		return 1;
	}
	std::string scripts = root + "/scripts";
	mkdirP(scripts);
	std::string cannedPath = scripts + "/canned.lua";
	std::string slowPath = scripts + "/slow.lua";
	std::string errorPath = scripts + "/erroring.lua";
	std::string offlinePath = scripts + "/offline.lua";
	std::string blockedPath = scripts + "/blocked.lua";
	std::string tooManyPath = scripts + "/toomany.lua";
	if (!writeFile(cannedPath, kCanned) || !writeFile(slowPath, kSlow) || !writeFile(errorPath, kErroring) ||
		!writeFile(offlinePath, kOffline) || !writeFile(blockedPath, kBlocked) || !writeFile(tooManyPath, kTooMany))
	{
		fprintf(stderr, "can't write the fixture scripts\n");
		return 1;
	}

	// 1. Every call comes back with its serial and the host's own structs, and bp.prefs reaches the state dir.
	{
		MangaWorker w;
		w.start(0, root);

		uint64_t popular = w.submit(job(MangaWorker::Job::Popular, "repo123/canned", cannedPath, 1));
		CHECK_TRUE(popular != 0);
		auto rs = waitFor(w, 1, 8000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1)
		{
			CHECK_EQ(rs[0].serial, popular);
			CHECK_EQ(rs[0].kind, MangaWorker::Job::Popular);
			CHECK_EQ(rs[0].sourceId, std::string("repo123/canned"));
			CHECK_TRUE(rs[0].ok);
			CHECK_EQ(rs[0].list.items.size(), (size_t)2);
			CHECK_EQ(rs[0].list.items[0].title, std::string("Title 1 p1"));
			// The item's own table comes back with it, for a later details()/chapters() (5.2).
			CHECK_TRUE(rs[0].list.items[0].json.find("\"extra\"") != std::string::npos);
			CHECK_TRUE(rs[0].list.more);
		}
		std::string prefs;
		std::string prefsPath = root + "/manga/prefs/repo123-canned.json";
		CHECK_TRUE(readFile(prefsPath, prefs));
		CHECK_TRUE(prefs.find("\"seen\"") != std::string::npos);

		std::string seriesJson = "{\"key\":\"/m/1\",\"title\":\"Title 1\",\"extra\":\"keep\"}";
		MangaWorker::Job details = job(MangaWorker::Job::Details, "repo123/canned", cannedPath);
		details.json = seriesJson;
		uint64_t detailsSerial = w.submit(details);
		rs = waitFor(w, 1, 8000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1)
		{
			CHECK_EQ(rs[0].serial, detailsSerial);
			CHECK_TRUE(rs[0].ok);
			CHECK_EQ(rs[0].details.title, std::string("Details /m/1"));
			CHECK_EQ(rs[0].details.status, std::string("ongoing"));
			CHECK_EQ(rs[0].details.genres.size(), (size_t)2);
		}

		MangaWorker::Job chapters = job(MangaWorker::Job::Chapters, "repo123/canned", cannedPath);
		chapters.json = seriesJson;
		uint64_t chaptersSerial = w.submit(chapters);
		rs = waitFor(w, 1, 8000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1)
		{
			CHECK_EQ(rs[0].serial, chaptersSerial);
			CHECK_TRUE(rs[0].ok);
			CHECK_EQ(rs[0].chapters.size(), (size_t)2);
			CHECK_EQ(rs[0].chapters[0].name, std::string("Chapter 2"));
			CHECK_EQ(rs[0].chapters[0].number, 2.0);
		}

		MangaWorker::Job pages = job(MangaWorker::Job::Pages, "repo123/canned", cannedPath);
		pages.json = "{\"key\":\"/c/2\",\"token\":\"tok2\"}";
		uint64_t pagesSerial = w.submit(pages);
		rs = waitFor(w, 1, 8000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1)
		{
			CHECK_EQ(rs[0].serial, pagesSerial);
			CHECK_TRUE(rs[0].ok);
			CHECK_EQ(rs[0].pages.size(), (size_t)2);
			CHECK_EQ(rs[0].pages[0].url, std::string("https://example.org/pages/tok2.jpg"));
			CHECK_EQ(rs[0].pages[1].key, std::string("p2"));
		}

		MangaWorker::Job image = job(MangaWorker::Job::Image, "repo123/canned", cannedPath);
		image.json = "{\"key\":\"p2\"}";
		uint64_t imageSerial = w.submit(image);
		rs = waitFor(w, 1, 8000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1)
		{
			CHECK_EQ(rs[0].serial, imageSerial);
			CHECK_TRUE(rs[0].ok);
			CHECK_EQ(rs[0].imageUrl, std::string("https://example.org/img/p2.jpg"));
			CHECK_EQ(rs[0].imageHeaders.size(), (size_t)1);
			if (rs[0].imageHeaders.size() == 1)
				CHECK_EQ(rs[0].imageHeaders[0].second, std::string("p2"));
		}

		uint64_t filtersSerial = w.submit(job(MangaWorker::Job::Filters, "repo123/canned", cannedPath));
		rs = waitFor(w, 1, 8000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1)
		{
			CHECK_EQ(rs[0].serial, filtersSerial);
			CHECK_TRUE(rs[0].ok);
			CHECK_TRUE(rs[0].filtersJson.find("\"status\"") != std::string::npos);
			CHECK_TRUE(rs[0].filtersJson.find("\"select\"") != std::string::npos);
		}

		MangaWorker::Job search = job(MangaWorker::Job::Search, "repo123/canned", cannedPath, 1);
		search.query = "dogs";
		search.json = "{\"status\":\"ongoing\"}";
		uint64_t searchSerial = w.submit(search);
		rs = waitFor(w, 1, 8000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1)
		{
			CHECK_EQ(rs[0].serial, searchSerial);
			CHECK_TRUE(rs[0].ok);
			CHECK_EQ(rs[0].list.items.size(), (size_t)1);
			CHECK_EQ(rs[0].list.items[0].title, std::string("dogs/1/ongoing"));
		}

		uint64_t latestSerial = w.submit(job(MangaWorker::Job::Latest, "repo123/canned", cannedPath, 2));
		rs = waitFor(w, 1, 8000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1)
		{
			CHECK_EQ(rs[0].serial, latestSerial);
			CHECK_TRUE(rs[0].ok);
			CHECK_EQ(rs[0].list.items.size(), (size_t)1);
			CHECK_EQ(rs[0].list.items[0].key, std::string("/l/2"));
		}

		// 2. A script's own error is an answer, not a hang: ok=false and its message.
		uint64_t errorSerial = w.submit(job(MangaWorker::Job::Popular, "repo123/erroring", errorPath, 1));
		rs = waitFor(w, 1, 8000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1)
		{
			CHECK_EQ(rs[0].serial, errorSerial);
			CHECK_TRUE(!rs[0].ok);
			CHECK_TRUE(rs[0].error.find("the site said no") != std::string::npos);
		}

		// 3. A request to a refused address fails cleanly and quickly, with the reason 5.4 gives the script.
		auto t0 = Clock::now();
		uint64_t offlineSerial = w.submit(job(MangaWorker::Job::Popular, "repo123/offline", offlinePath, 1));
		rs = waitFor(w, 1, 10000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1)
		{
			CHECK_EQ(rs[0].serial, offlineSerial);
			CHECK_TRUE(!rs[0].ok);
			CHECK_TRUE(!rs[0].error.empty());
			// The host wraps a failed transfer as "bp.http: <reason>", so the page has a line to show (5.4).
			CHECK_TRUE(rs[0].error.find("bp.http") != std::string::npos);
			CHECK_TRUE(rs[0].error.find("Blocked") != std::string::npos);
		}
		CHECK_TRUE(msSince(t0) < 5000);

		// 3b. The card itself and the LAN: localhost, 10/8, [::1] and the metadata address are all refused.
		uint64_t blockedSerial = w.submit(job(MangaWorker::Job::Popular, "repo123/blocked", blockedPath, 1));
		rs = waitFor(w, 1, 10000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1)
		{
			CHECK_EQ(rs[0].serial, blockedSerial);
			CHECK_TRUE(rs[0].ok);
			if (!rs[0].ok) fprintf(stderr, "  blocked fixture: %s\n", rs[0].error.c_str());
		}

		// 3c. 5.4's 40 per call: the 41st request fails the call even though its address is refused too.
		uint64_t tooManySerial = w.submit(job(MangaWorker::Job::Popular, "repo123/toomany", tooManyPath, 1));
		rs = waitFor(w, 1, 15000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1)
		{
			CHECK_EQ(rs[0].serial, tooManySerial);
			CHECK_TRUE(!rs[0].ok);
			CHECK_TRUE(rs[0].error.find("40") != std::string::npos);
		}

		w.stop();
		std::this_thread::sleep_for(std::chrono::milliseconds(200));	// the detached thread leaves in its own time
	}

	// 4. A newer job for the same source and kind replaces a waiting older one: page 2 never runs.
	{
		MangaWorker w;
		w.start(0, root);
		uint64_t blocker = w.submit(job(MangaWorker::Job::Popular, "repo123/slow", slowPath, 1));
		// Wait until the blocker has opened its source, so it is running rather than still waiting: only a waiting
		// job is replaced, and a submit racing the thread's start would otherwise replace the blocker itself.
		auto t0 = Clock::now();
		while (w.openSourceCount() == 0 && msSince(t0) < 5000)
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		CHECK_EQ(w.openSourceCount(), 1);
		uint64_t page2 = w.submit(job(MangaWorker::Job::Popular, "repo123/slow", slowPath, 2));
		uint64_t page3 = w.submit(job(MangaWorker::Job::Popular, "repo123/slow", slowPath, 3));
		auto rs = waitFor(w, 2, 15000);
		CHECK_EQ(rs.size(), (size_t)2);
		CHECK_TRUE(hasSerial(rs, blocker));
		CHECK_TRUE(hasSerial(rs, page3));
		CHECK_TRUE(!hasSerial(rs, page2));
		const MangaWorker::Result* third = bySerial(rs, page3);
		CHECK_TRUE(third != nullptr);
		if (third && !third->list.items.empty())
		{
			CHECK_TRUE(third->list.items[0].title.find("p3") != std::string::npos);
		}
		w.stop();
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}

	// 5. cancelAll empties the queue and drops the call in flight; the worker stays usable.
	{
		MangaWorker w;
		w.start(0, root);
		w.submit(job(MangaWorker::Job::Popular, "repo123/slow", slowPath, 1));	// running
		w.submit(job(MangaWorker::Job::Latest, "repo123/slow", slowPath, 1));	// waiting
		w.submit(job(MangaWorker::Job::Details, "repo123/slow", slowPath, 1));	// waiting
		w.cancelAll();
		auto rs = drainFor(w, 1500);
		CHECK_EQ(rs.size(), (size_t)0);

		uint64_t again = w.submit(job(MangaWorker::Job::Popular, "repo123/canned", cannedPath, 2));
		CHECK_TRUE(again != 0);
		rs = waitFor(w, 1, 8000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1)
		{
			CHECK_EQ(rs[0].serial, again);
			CHECK_TRUE(rs[0].ok);
			CHECK_TRUE(rs[0].list.items[0].title.find("p2") != std::string::npos);
		}
		w.stop();
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}

	// 6. The idle close: the state goes after the idle time, and the next call reloads it.
	{
		MangaWorker w;
		w.setIdleSeconds(0.5);
		w.start(0, root);
		uint64_t first = w.submit(job(MangaWorker::Job::Popular, "repo123/canned", cannedPath, 1));
		auto rs = waitFor(w, 1, 8000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1) CHECK_EQ(rs[0].serial, first);
		CHECK_EQ(w.openSourceCount(), 1);
		std::this_thread::sleep_for(std::chrono::milliseconds(1500));
		CHECK_EQ(w.openSourceCount(), 0);
		uint64_t later = w.submit(job(MangaWorker::Job::Latest, "repo123/canned", cannedPath, 1));
		rs = waitFor(w, 1, 8000);
		CHECK_EQ(rs.size(), (size_t)1);
		if (rs.size() == 1)
		{
			CHECK_EQ(rs[0].serial, later);
			CHECK_TRUE(rs[0].ok);
		}
		w.stop();
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}

	// 7. The validator MangaRepos takes: the fixture passes, a wrong id or a broken contract is refused, and a
	// script that loops at load costs about its second, not the worker's 30.
	{
		std::string error;
		CHECK_TRUE(MangaWorker::validateScript(kCanned, "canned", error));
		CHECK_TRUE(error.empty());

		CHECK_TRUE(!MangaWorker::validateScript(kCanned, "another-id", error));
		CHECK_TRUE(!error.empty());

		CHECK_TRUE(!MangaWorker::validateScript("this isn't lua", "broken", error));
		CHECK_TRUE(!error.empty());

		CHECK_TRUE(!MangaWorker::validateScript(
			"return { api = 1, id = \"nofns\", name = \"No functions\" }", "nofns", error));
		CHECK_TRUE(error.find("popular") != std::string::npos);

		CHECK_TRUE(!MangaWorker::validateScript(
			"return { api = 2, id = \"newer\", name = \"Newer\" }", "newer", error));
		CHECK_TRUE(error.find("newer") != std::string::npos);

		auto t0 = Clock::now();
		CHECK_TRUE(!MangaWorker::validateScript("local x = 0 while true do x = x + 1 end", "loop", error));
		CHECK_TRUE(msSince(t0) < 3000);
	}

	if (failures)
	{
		fprintf(stderr, "worker: %d failure(s)\n", failures);
		return 1;
	}
	printf("worker: all passed\n");
	return 0;
}
