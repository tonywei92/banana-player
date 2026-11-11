// The manga source-script host: the 5.2 calls, the 5.2.1 filters, the 5.3 refusals and the 5.4 sandbox and limits.
#include "mangasource.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <unistd.h>

using namespace bp;

static int failures = 0;
#define CHECK_EQ(a, b) \
	do { auto va = (a); auto vb = (b); \
		if (!(va == vb)) { failures++; fprintf(stderr, "%s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); } } while (0)
#define CHECK_TRUE(c) \
	do { if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static size_t countOf(const std::string& haystack, const std::string& needle)
{
	size_t n = 0, p = 0;
	while ((p = haystack.find(needle, p)) != std::string::npos)
	{
		n++;
		p += needle.size();
	}
	return n;
}

static std::string tempPath(const std::string& name)
{
	const char* dir = std::getenv("TMPDIR");
	std::string d = dir && *dir ? dir : "/tmp";
	if (!d.empty() && d.back() == '/') d.pop_back();
	return d + "/bpmanga-" + std::to_string((long)getpid()) + "-" + name;
}

// A source that exercises every call against a fake site.
static const char* kFixture = R"LUA(
local base = "https://example.org"

return {
  api = 1,
  id = "fixture-reader",
  name = "Fixture Reader",
  lang = "en",
  version = "1.0.0",
  nsfw = false,
  baseUrl = base,
  headers = { ["Referer"] = base .. "/" },
  prefs = {
    { id = "quality", type = "select", name = "Image quality",
      options = { "High", "Data saver" }, default = "High" },
  },

  popular = function(page)
    local r = bp.json.decode(bp.http.get(base .. "/api/popular?page=" .. page,
      { headers = { ["Accept"] = "application/json" }, timeout = 5 }).body)
    local items = {}
    for _, x in ipairs(r.items) do
      items[#items + 1] = { key = x.key, title = x.title, cover = x.cover, extra = "keep-me" }
    end
    return { items = items, more = r.more }
  end,

  latest = function(page)
    return bp.json.decode(bp.http.get(base .. "/api/latest?page=" .. page).body)
  end,

  search = function(query, page, filters)
    local r = bp.json.decode(bp.http.post(base .. "/api/search", nil,
      { form = { q = query, page = tostring(page) }, headers = { ["X-Test"] = "1" } }).body)
    return {
      items = { { key = "/search/" .. page,
        title = r.title .. "/" .. tostring(filters and filters.status or "none"), cover = "" } },
      more = false,
    }
  end,

  filters = function()
    local g = bp.json.decode(bp.http.get(base .. "/api/genres").body)
    local opts = {}
    for _, x in ipairs(g) do opts[#opts + 1] = { id = x.slug, name = x.name } end
    return {
      { id = "sort", type = "sort", name = "Sort",
        options = { { id = "popular", name = "Popular" }, { id = "latest", name = "Latest" } },
        default = { by = "popular", asc = false } },
      { id = "status", type = "select", name = "Status",
        options = { { id = "any", name = "Any" }, { id = "ongoing", name = "Ongoing" } } },
      { id = "genres", type = "tri", name = "Genres", options = opts },
    }
  end,

  details = function(manga)
    return {
      title = "Series " .. tostring(manga.key),
      author = tostring(manga.extra or "none"),
      artist = "Artist",
      description = "A description",
      genres = { "Action", "Drama" },
      status = "ongoing",
      cover = base .. "/cover.jpg",
    }
  end,

  chapters = function(manga)
    return {
      { key = "/c/2", name = "Chapter 2", number = 2, date = 1731242096, group = "Scan", token = "tok2" },
      { key = "/c/1", name = "Chapter 1", number = 1, date = 1731155696, group = "" },
    }
  end,

  pages = function(chapter)
    return { { url = base .. "/pages/" .. tostring(chapter.token or "1") .. ".jpg" }, { key = "p2" } }
  end,

  image = function(page)
    return { url = base .. "/img/" .. page.key .. ".jpg", headers = { ["X-Page"] = page.key } }
  end,
}
)LUA";

static const char* kMinimal = "return { api = 1, id = \"minimal\", name = \"Minimal\", "
	"popular = function() return { items = {}, more = false } end, "
	"details = function() return {} end, chapters = function() return {} end, "
	"pages = function() return {} end }";

struct FakeHttp
{
	int calls = 0;
	std::string method, url, body;
	std::map<std::string, std::string> headers;
	int timeout = 0;

	HttpResult respond(const std::string& m, const std::string& u,
		const std::map<std::string, std::string>& h, const std::string& b, int t)
	{
		calls++;
		method = m;
		url = u;
		body = b;
		headers = h;
		timeout = t;
		HttpResult r;
		r.status = 200;
		r.url = u;
		if (u.find("/api/popular") != std::string::npos)
			r.body = "{\"items\":[{\"key\":\"/m/1\",\"title\":\"First\",\"cover\":\"https://example.org/1.jpg\"},"
				"{\"key\":\"/m/2\",\"title\":\"Second\",\"cover\":\"https://example.org/2.jpg\"}],"
				"\"more\":true}";
		else if (u.find("/api/latest") != std::string::npos)
			r.body = "{\"items\":[{\"key\":\"/l/1\",\"title\":\"Latest\",\"cover\":\"\"}],\"more\":false}";
		else if (u.find("/api/search") != std::string::npos)
			r.body = "{\"title\":\"found\"}";
		else if (u.find("/api/genres") != std::string::npos)
			r.body = "[{\"slug\":\"action\",\"name\":\"Action\"},{\"slug\":\"romance\",\"name\":\"Romance\"}]";
		else
		{
			r.status = 404;
			r.body = "{}";
		}
		return r;
	}
};

static void testCalls()
{
	SourceHost host;
	FakeHttp fake;
	host.setHttp([&fake](const std::string& m, const std::string& u,
		const std::map<std::string, std::string>& h, const std::string& b, int t) {
		return fake.respond(m, u, h, b, t);
	});
	std::string err;
	CHECK_TRUE(host.load(kFixture, "fixture-reader", err));
	CHECK_TRUE(err.empty());
	CHECK_TRUE(host.loaded());
	if (!host.loaded()) return;

	const SourceMeta& m = host.meta();
	CHECK_EQ(m.id, std::string("fixture-reader"));
	CHECK_EQ(m.name, std::string("Fixture Reader"));
	CHECK_EQ(m.lang, std::string("en"));
	CHECK_EQ(m.version, std::string("1.0.0"));
	CHECK_EQ(m.nsfw, false);
	CHECK_EQ(m.baseUrl, std::string("https://example.org"));
	CHECK_EQ(m.headers.size(), size_t(1));
	CHECK_EQ(m.headers.at("Referer"), std::string("https://example.org/"));
	CHECK_EQ(m.prefs.size(), size_t(1));
	CHECK_EQ(m.prefs[0].id, std::string("quality"));
	CHECK_EQ(m.prefs[0].type, std::string("select"));
	CHECK_EQ(m.prefs[0].name, std::string("Image quality"));
	CHECK_EQ(m.prefs[0].options.size(), size_t(2));
	CHECK_EQ(m.prefs[0].options[1], std::string("Data saver"));
	CHECK_EQ(m.prefs[0].def, std::string("High"));

	MangaList list;
	CHECK_TRUE(host.popular(1, list, err));
	CHECK_EQ(list.items.size(), size_t(2));
	CHECK_EQ(list.items[0].key, std::string("/m/1"));
	CHECK_EQ(list.items[0].title, std::string("First"));
	CHECK_EQ(list.items[0].cover, std::string("https://example.org/1.jpg"));
	CHECK_TRUE(list.items[0].json.find("keep-me") != std::string::npos);
	CHECK_EQ(list.more, true);
	CHECK_EQ(fake.method, std::string("GET"));
	CHECK_EQ(fake.url, std::string("https://example.org/api/popular?page=1"));
	CHECK_EQ(fake.timeout, 5);
	CHECK_EQ(fake.headers["Referer"], std::string("https://example.org/"));
	CHECK_EQ(fake.headers["Accept"], std::string("application/json"));

	MangaList latest;
	CHECK_TRUE(host.latest(3, latest, err));
	CHECK_EQ(latest.items.size(), size_t(1));
	CHECK_EQ(latest.items[0].key, std::string("/l/1"));
	CHECK_EQ(fake.url, std::string("https://example.org/api/latest?page=3"));

	MangaList found;
	CHECK_TRUE(host.search("a b", 2, "{\"status\":\"ongoing\"}", found, err));
	CHECK_EQ(found.items.size(), size_t(1));
	CHECK_EQ(found.items[0].title, std::string("found/ongoing"));
	CHECK_EQ(fake.method, std::string("POST"));
	CHECK_EQ(fake.body, std::string("page=2&q=a%20b"));
	CHECK_EQ(fake.headers["X-Test"], std::string("1"));

	std::string filtersJson;
	CHECK_TRUE(host.filters(filtersJson, err));
	CHECK_TRUE(filtersJson.find("\"id\":\"genres\"") != std::string::npos);
	CHECK_TRUE(filtersJson.find("\"type\":\"tri\"") != std::string::npos);
	CHECK_TRUE(filtersJson.find("\"name\":\"Romance\"") != std::string::npos);
	CHECK_TRUE(filtersJson.find("\"by\":\"popular\"") != std::string::npos);

	MangaDetails details;
	CHECK_TRUE(host.details(list.items[0].json, details, err));
	CHECK_EQ(details.title, std::string("Series /m/1"));
	CHECK_EQ(details.author, std::string("keep-me"));	// the item's extra field came back through JSON
	CHECK_EQ(details.artist, std::string("Artist"));
	CHECK_EQ(details.description, std::string("A description"));
	CHECK_EQ(details.status, std::string("ongoing"));
	CHECK_EQ(details.genres.size(), size_t(2));
	CHECK_EQ(details.genres[0], std::string("Action"));
	CHECK_EQ(details.cover, std::string("https://example.org/cover.jpg"));

	std::vector<MangaChapter> chapters;
	CHECK_TRUE(host.chapters(list.items[0].json, chapters, err));
	CHECK_EQ(chapters.size(), size_t(2));
	CHECK_EQ(chapters[0].key, std::string("/c/2"));
	CHECK_EQ(chapters[0].name, std::string("Chapter 2"));
	CHECK_EQ(chapters[0].number, 2.0);
	CHECK_EQ(chapters[0].date, int64_t(1731242096));
	CHECK_EQ(chapters[0].group, std::string("Scan"));
	CHECK_TRUE(chapters[0].json.find("tok2") != std::string::npos);

	std::vector<MangaPage> pages;
	CHECK_TRUE(host.pages(chapters[0].json, pages, err));
	CHECK_EQ(pages.size(), size_t(2));
	CHECK_EQ(pages[0].url, std::string("https://example.org/pages/tok2.jpg"));
	CHECK_EQ(pages[1].key, std::string("p2"));

	MangaImage image;
	CHECK_TRUE(host.image(pages[1].json, image, err));
	CHECK_EQ(image.url, std::string("https://example.org/img/p2.jpg"));
	CHECK_EQ(image.headers["X-Page"], std::string("p2"));
	CHECK_EQ(image.headers["Referer"], std::string("https://example.org/"));
}

static void testOptionalAndRefusals()
{
	std::string err;
	{
		SourceHost h;
		CHECK_TRUE(h.load(kMinimal, "minimal", err));
		MangaList list;
		CHECK_TRUE(!h.latest(1, list, err));
		CHECK_TRUE(err.find("latest") != std::string::npos);
		CHECK_TRUE(h.popular(1, list, err));	// one missing optional call doesn't break the state
	}
	{
		SourceHost h;
		CHECK_TRUE(!h.load(kMinimal, "wrong-id", err));
		CHECK_TRUE(!err.empty());
	}
	{
		std::string api2 = kMinimal;
		size_t p = api2.find("api = 1");
		api2.replace(p, 7, "api = 2");
		SourceHost h;
		CHECK_TRUE(!h.load(api2, "minimal", err));
		CHECK_TRUE(err.find("needs a newer Banana Player") != std::string::npos);
	}
	{
		std::string noPages = kMinimal;
		size_t p = noPages.find(", pages = function() return {} end");
		noPages.erase(p, std::strlen(", pages = function() return {} end"));
		SourceHost h;
		CHECK_TRUE(!h.load(noPages, "minimal", err));
		CHECK_TRUE(err.find("pages") != std::string::npos);
	}
	{
		SourceHost h;
		std::string bytecode("\x1bLua\x54\x00", 6);
		CHECK_TRUE(!h.load(bytecode, "minimal", err));
		CHECK_TRUE(!err.empty());
	}
	{
		SourceHost h;
		CHECK_TRUE(!h.load("return 42", "minimal", err));
		CHECK_TRUE(err.find("table") != std::string::npos);
	}
}

static bool escapeFails(const std::string& body)
{
	std::string script = "return { api = 1, id = \"escape\", name = \"Escape\", "
		"popular = function() " + body + " end, "
		"details = function() return {} end, chapters = function() return {} end, "
		"pages = function() return {} end }";
	SourceHost h;
	std::string err;
	MangaList list;
	if (!h.load(script, "escape", err)) return false;	// a sandbox that leaks at load time is a failure too
	return !h.popular(1, list, err);
}

static void testSandbox()
{
	CHECK_TRUE(escapeFails("return io.open(\"/etc/passwd\")"));
	CHECK_TRUE(escapeFails("return os.execute(\"true\")"));
	CHECK_TRUE(escapeFails("return require(\"x\")"));
	CHECK_TRUE(escapeFails("return load(\"return 1\")"));
	CHECK_TRUE(escapeFails("return string.dump(function() end)"));
	CHECK_TRUE(escapeFails("return debug.getinfo(1)"));
	CHECK_TRUE(escapeFails("return collectgarbage(\"collect\")"));
	CHECK_TRUE(escapeFails("return loadfile(\"/etc/passwd\")"));
	CHECK_TRUE(escapeFails("return dofile(\"/etc/passwd\")"));
	// collectgarbage("count") is the one allowed form.
	CHECK_TRUE(!escapeFails("local n = collectgarbage(\"count\") "
		"return { items = { { key = tostring(n), title = \"\", cover = \"\" } }, more = false }"));
}

static const char* kLoop = R"LUA(
return {
  api = 1, id = "loop", name = "Loop",
  popular = function(page)
    if page == 1 then while true do end end
    return { items = { { key = "/ok", title = "ok", cover = "" } }, more = false }
  end,
  details = function() return {} end,
  chapters = function() return {} end,
  pages = function() return {} end,
}
)LUA";

static const char* kCoroutineLoop = R"LUA(
return {
  api = 1, id = "coroutine", name = "Coroutine",
  popular = function()
    local co = coroutine.wrap(function() while true do end end)
    co()
  end,
  details = function() return {} end,
  chapters = function() return {} end,
  pages = function() return {} end,
}
)LUA";

static const char* kMemoryBomb = R"LUA(
local keep = {}
return {
  api = 1, id = "bomb", name = "Bomb",
  popular = function()
    while true do keep[#keep + 1] = string.rep("x", 65536) end
  end,
  details = function() return {} end,
  chapters = function() return {} end,
  pages = function() return {} end,
}
)LUA";

static void testLimits()
{
	std::string err;
	{
		SourceHost h;
		h.setLimits(5.0, 1000000, 8u * 1024u * 1024u);
		CHECK_TRUE(h.load(kLoop, "loop", err));
		MangaList list;
		CHECK_TRUE(!h.popular(1, list, err));
		CHECK_TRUE(err.find("instruction") != std::string::npos);
		CHECK_TRUE(h.popular(2, list, err));	// the host reloaded its state for the next call
		CHECK_EQ(list.items.size(), size_t(1));
		CHECK_EQ(list.items[0].key, std::string("/ok"));
	}
	{
		SourceHost h;
		h.setLimits(0.05, 1000000000, 8u * 1024u * 1024u);
		CHECK_TRUE(h.load(kLoop, "loop", err));
		MangaList list;
		CHECK_TRUE(!h.popular(1, list, err));
		CHECK_TRUE(err.find("time") != std::string::npos);
		CHECK_TRUE(h.popular(2, list, err));
	}
	{
		SourceHost h;
		h.setLimits(5.0, 1000000, 8u * 1024u * 1024u);
		CHECK_TRUE(h.load(kCoroutineLoop, "coroutine", err));
		MangaList list;
		CHECK_TRUE(!h.popular(1, list, err));
		CHECK_TRUE(err.find("instruction") != std::string::npos);
	}
	{
		SourceHost h;
		h.setLimits(5.0, 1000000000, 2u * 1024u * 1024u);
		CHECK_TRUE(h.load(kMemoryBomb, "bomb", err));
		MangaList list;
		CHECK_TRUE(!h.popular(1, list, err));
		CHECK_TRUE(err.find("memory") != std::string::npos);
	}
}

static const char* kPrefs = R"LUA(
return {
  api = 1, id = "prefs", name = "Prefs",
  popular = function(page)
    if page == 99 then
      bp.prefs.set("token", "abc")
    elseif page == 98 then
      bp.prefs.set("big", string.rep("x", 70000))
    else
      return { items = { { key = "tok", title = bp.prefs.get("token") or "none", cover = "" } },
        more = false }
    end
    return { items = {}, more = false }
  end,
  details = function() return {} end,
  chapters = function() return {} end,
  pages = function() return {} end,
}
)LUA";

static void testPrefs()
{
	std::string path = tempPath("prefs.json");
	std::remove(path.c_str());
	std::string err;
	{
		SourceHost h;
		h.setPrefsPath(path);
		CHECK_TRUE(h.load(kPrefs, "prefs", err));
		MangaList list;
		CHECK_TRUE(h.popular(99, list, err));
		CHECK_TRUE(h.popular(1, list, err));
		CHECK_EQ(list.items.size(), size_t(1));
		CHECK_EQ(list.items[0].title, std::string("abc"));
	}
	{
		SourceHost h;
		h.setPrefsPath(path);
		CHECK_TRUE(h.load(kPrefs, "prefs", err));
		MangaList list;
		CHECK_TRUE(h.popular(1, list, err));
		CHECK_EQ(list.items.size(), size_t(1));
		CHECK_EQ(list.items[0].title, std::string("abc"));
	}
	{
		std::ifstream in(path, std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		CHECK_TRUE(text.find("\"token\":\"abc\"") != std::string::npos);
	}
	{
		SourceHost h;
		h.setPrefsPath(path);
		CHECK_TRUE(h.load(kPrefs, "prefs", err));
		MangaList list;
		CHECK_TRUE(!h.popular(98, list, err));
		CHECK_TRUE(err.find("64 KB") != std::string::npos);
	}
	std::remove(path.c_str());
	std::remove((path + ".tmp").c_str());
}

static const char* kJson = R"LUA(
return {
  api = 1, id = "json", name = "Json",
  popular = function(page)
    if page == 1 then
      local t = { a = { 1, 2, { b = "x" } }, e = {}, n = 1.5, s = "caf\u{e9}" }
      local back = bp.json.decode(bp.json.encode(t))
      return { items = { { key = back.a[2] .. "|" .. back.a[3].b .. "|" .. type(back.e) .. "|" ..
        tostring(back.n) .. "|" .. back.s, title = "", cover = "" } }, more = false }
    elseif page == 2 then
      local v = bp.json.decode('{"a":[1,2,{"b":"x\\u00e9"}],"e":{},"n":1.5}')
      return { items = { { key = v.a[1] .. "|" .. v.a[3].b .. "|" .. type(v.e) .. "|" ..
        tostring(v.n), title = "", cover = "" } }, more = false }
    end
    return { items = { { key = bp.json.encode({}), title = bp.json.encode({ 1, 2 }), cover = "" } },
      more = false }
  end,
  details = function() return {} end,
  chapters = function() return {} end,
  pages = function() return {} end,
}
)LUA";

static void testJson()
{
	SourceHost h;
	std::string err;
	CHECK_TRUE(h.load(kJson, "json", err));
	MangaList list;
	CHECK_TRUE(h.popular(1, list, err));
	CHECK_EQ(list.items.size(), size_t(1));
	CHECK_EQ(list.items[0].key, std::string("2|x|table|1.5|caf\xC3\xA9"));
	CHECK_TRUE(h.popular(2, list, err));
	CHECK_EQ(list.items[0].key, std::string("1|x\xC3\xA9|table|1.5"));
	CHECK_TRUE(h.popular(3, list, err));
	CHECK_EQ(list.items[0].key, std::string("{}"));
	CHECK_EQ(list.items[0].title, std::string("[1,2]"));
}

static const char* kUrl = R"LUA(
return {
  api = 1, id = "url", name = "Url",
  popular = function()
    local v = {
      bp.url.join("https://example.org/a/b", "c"),
      bp.url.join("https://example.org/a/b/c", "../d"),
      bp.url.join("https://example.org/a/b", "/x"),
      bp.url.join("https://example.org/a/b?x=1", "?y=2"),
      bp.url.join("https://example.org/a/b", "https://other.example/z"),
      bp.url.encode("a b/\u{e9}"),
      bp.url.decode("a%20b%2F%C3%A9+r"),
      bp.url.query({ b = "x y", a = "1" }),
      bp.url.join("https://example.org/a//b", "c"),
    }
    local items = {}
    for i = 1, #v do items[#items + 1] = { key = v[i], title = tostring(i), cover = "" } end
    return { items = items, more = false }
  end,
  details = function() return {} end,
  chapters = function() return {} end,
  pages = function() return {} end,
}
)LUA";

static void testUrl()
{
	SourceHost h;
	std::string err;
	CHECK_TRUE(h.load(kUrl, "url", err));
	MangaList list;
	CHECK_TRUE(h.popular(1, list, err));
	CHECK_EQ(list.items.size(), size_t(9));
	CHECK_EQ(list.items[0].key, std::string("https://example.org/a/c"));
	CHECK_EQ(list.items[1].key, std::string("https://example.org/a/d"));
	CHECK_EQ(list.items[2].key, std::string("https://example.org/x"));
	CHECK_EQ(list.items[3].key, std::string("https://example.org/a/b?y=2"));
	CHECK_EQ(list.items[4].key, std::string("https://other.example/z"));
	CHECK_EQ(list.items[5].key, std::string("a%20b%2F%C3%A9"));
	CHECK_EQ(list.items[6].key, std::string("a b/\xC3\xA9+r"));
	CHECK_EQ(list.items[7].key, std::string("a=1&b=x%20y"));
	CHECK_EQ(list.items[8].key, std::string("https://example.org/a//c"));
}

static const char* kDate = R"LUA(
return {
  api = 1, id = "date", name = "Date",
  popular = function()
    local v = {
      bp.date("2024-11-10T12:34:56Z"),
      bp.date("2024-11-10", "%Y-%m-%d"),
      bp.date("2024-11-10 12:34:56", "%Y-%m-%d %H:%M:%S"),
      bp.date("not a date", "%Y-%m-%d"),
      bp.date("2024-11-10T12:34:56+02:00"),
    }
    local items = {}
    for i = 1, #v do items[#items + 1] = { key = tostring(v[i]), title = tostring(i), cover = "" } end
    return { items = items, more = false }
  end,
  details = function() return {} end,
  chapters = function() return {} end,
  pages = function() return {} end,
}
)LUA";

static void testDate()
{
	SourceHost h;
	std::string err;
	CHECK_TRUE(h.load(kDate, "date", err));
	MangaList list;
	CHECK_TRUE(h.popular(1, list, err));
	CHECK_EQ(list.items.size(), size_t(5));
	CHECK_EQ(list.items[0].key, std::string("1731242096"));
	CHECK_EQ(list.items[1].key, std::string("1731196800"));
	CHECK_EQ(list.items[2].key, std::string("1731242096"));
	CHECK_EQ(list.items[3].key, std::string("nil"));
	CHECK_EQ(list.items[4].key, std::string("1731234896"));
}

static const char* kFilterLimits = R"LUA(
return {
  api = 1, id = "filter-limits", name = "Filter limits",
  filters = function()
    local opts = {}
    for i = 1, 2100 do opts[i] = { id = "o" .. i, name = "Option " .. i } end
    local list = {}
    for i = 1, 60 do list[i] = { id = "f" .. i, type = "select", name = string.rep("N", 70) } end
    list[1] = { id = "bogus", type = "nope", name = "Bad" }
    list[2] = { id = "long", type = "select", name = string.rep("L", 70), options = opts }
    return list
  end,
  popular = function() return { items = {}, more = false } end,
  details = function() return {} end,
  chapters = function() return {} end,
  pages = function() return {} end,
}
)LUA";

static void testFilterLimits()
{
	SourceHost h;
	std::string err;
	CHECK_TRUE(h.load(kFilterLimits, "filter-limits", err));
	std::string json;
	CHECK_TRUE(h.filters(json, err));
	CHECK_EQ(countOf(json, "\"type\":"), size_t(50));
	CHECK_EQ(countOf(json, "\"id\":\"o"), size_t(2000));
	CHECK_TRUE(json.find("bogus") == std::string::npos);
	CHECK_TRUE(json.find(std::string(60, 'L')) != std::string::npos);
	CHECK_TRUE(json.find(std::string(61, 'L')) == std::string::npos);
	CHECK_TRUE(json.find(std::string(60, 'N')) != std::string::npos);
	CHECK_TRUE(json.find(std::string(61, 'N')) == std::string::npos);
}

static const char* kHtml = R"LUA(
local listPage = '<html><head><title>Popular</title></head><body><ul id="list">'
  .. '<li class="item"><a href="/m/1" data-id="1"><img data-src="https://example.org/1.jpg"><span class="t">First</span></a></li>'
  .. '<li class="item"><a href="/m/2" data-id="2"><img data-src="https://example.org/2.jpg"><span class="t">Second</span></a></li>'
  .. '<li class="item"><a href="/m/3"><span class="t">Third &amp; more</span></a></li>'
  .. '</ul></body></html>'

local function try(f)
  local ok, e = pcall(f)
  if ok then return "NO ERROR" end
  return tostring(e)
end

return {
  api = 1, id = "html", name = "Html",
  popular = function(page)
    if page == 1 then	-- a list page read the way a source reads one
      local doc = bp.html.parse(listPage)
      local items = {}
      for _, li in ipairs(doc:select("li.item")) do
        local a = li:selectFirst("a")
        local img = li:selectFirst("img")
        items[#items + 1] = {
          key = a:attr("href"),
          title = li:selectFirst("span.t"):text(),
          cover = img and img:attr("data-src") or "",
        }
      end
      return { items = items, more = false }
    elseif page == 2 then	-- text / attr / html / tag / nested select, exact strings
      local doc = bp.html.parse(listPage)
      local li = doc:selectFirst("li.item")
      local a = li:selectFirst("a")
      local parts = {
        a:tag(), a:attr("href"), tostring(a:attr("nothing")),
        tostring(#li:select("a")), a:selectFirst("img"):attr("data-src"),
        doc:selectFirst("title"):text(), li:selectFirst("span"):html(),
      }
      return { items = { { key = table.concat(parts, ";"), title = "", cover = "" } }, more = false }
    elseif page == 3 then	-- the document's Lua name dies every round; the elements must still read
      local keep = {}
      for i = 1, 16 do
        local doc = bp.html.parse(listPage)
        keep[i] = doc:selectFirst("a")
        local junk = string.rep("g", 100000)	-- churn, so dead documents really do get collected
        if junk == nil then return { items = {}, more = false } end
      end
      local n = 0
      for i = 1, 16 do
        if keep[i]:attr("href") == "/m/1" and keep[i]:text() == "First" and keep[i]:tag() == "a" then
          n = n + 1
        end
      end
      return { items = { { key = n .. "|" .. keep[8]:html(), title = "", cover = "" } }, more = false }
    elseif page == 4 then	-- every wrong argument type says so, and nothing crashes
      local doc = bp.html.parse('<p class="c">y</p>')
      local el = doc:selectFirst("p")
      local parts = {
        try(function() return bp.html.parse() end),
        try(function() return bp.html.parse({}) end),
        try(function() return doc:select() end),
        try(function() return doc:select({}) end),
        try(function() return el.attr(el) end),
        try(function() return el:select(el) end),
        try(function() return el.text(doc) end),
        try(function() return doc.selectFirst(el, "p") end),
      }
      return { items = { { key = table.concat(parts, "\n"), title = "", cover = "" } }, more = false }
    elseif page == 5 then	-- a runaway page is refused whole
      local ok, e = pcall(bp.html.parse, string.rep("x", 8 * 1024 * 1024 + 1))
      return { items = { { key = ok and "NO ERROR" or tostring(e), title = "", cover = "" } }, more = false }
    elseif page == 6 then	-- select honours HtmlDoc's cap and comes back with it, not more
      local doc = bp.html.parse(string.rep('<div class="c"></div>', 20001))
      return { items = { { key = tostring(#doc:select("div.c")), title = "", cover = "" } }, more = false }
    end
    return { items = {}, more = false }
  end,
  details = function() return {} end,
  chapters = function() return {} end,
  pages = function() return {} end,
}
)LUA";

static void testHtml()
{
	SourceHost h;
	std::string err;
	CHECK_TRUE(h.load(kHtml, "html", err));
	MangaList list;

	CHECK_TRUE(h.popular(1, list, err));
	CHECK_EQ(list.items.size(), size_t(3));
	CHECK_EQ(list.items[0].key, std::string("/m/1"));
	CHECK_EQ(list.items[0].title, std::string("First"));
	CHECK_EQ(list.items[0].cover, std::string("https://example.org/1.jpg"));
	CHECK_EQ(list.items[1].key, std::string("/m/2"));
	CHECK_EQ(list.items[2].key, std::string("/m/3"));
	CHECK_EQ(list.items[2].title, std::string("Third & more"));	// the entity came back resolved
	CHECK_EQ(list.items[2].cover, std::string(""));	// an item without an img

	CHECK_TRUE(h.popular(2, list, err));
	CHECK_EQ(list.items[0].key, std::string(
		"a;/m/1;nil;1;https://example.org/1.jpg;Popular;<span class=\"t\">First</span>"));

	CHECK_TRUE(h.popular(3, list, err));	// every element still reads its document
	CHECK_EQ(list.items.size(), size_t(1));
	CHECK_TRUE(list.items[0].key.compare(0, 3, "16|") == 0);
	CHECK_TRUE(list.items[0].key.find("href=\"/m/1\"") != std::string::npos);

	CHECK_TRUE(h.popular(4, list, err));
	const std::string& errs = list.items[0].key;
	CHECK_TRUE(errs.find("NO ERROR") == std::string::npos);
	CHECK_TRUE(errs.find("string expected") != std::string::npos);
	CHECK_TRUE(errs.find("no value") != std::string::npos);
	CHECK_TRUE(errs.find("element expected") != std::string::npos);
	CHECK_TRUE(errs.find("bp.html.doc expected") != std::string::npos);

	CHECK_TRUE(h.popular(5, list, err));
	CHECK_TRUE(list.items[0].key.find("larger than 8 MB") != std::string::npos);

	CHECK_TRUE(h.popular(6, list, err));
	CHECK_EQ(list.items[0].key, std::string("20000"));
}

static const char* kHtmlMemory = R"LUA(
local page = string.rep("<p>x</p>", 100000)   -- 700 KB; every document is charged a few MB against the cap
local keep = {}

return {
  api = 1, id = "html-memory", name = "Html Memory",
  popular = function(n)
    for i = 1, 100 do keep[i] = bp.html.parse(page) end
    return { items = { { key = tostring(#keep), title = "", cover = "" } }, more = false }
  end,
  details = function() return {} end,
  chapters = function() return {} end,
  pages = function() return {} end,
}
)LUA";

static void testHtmlMemory()
{
	SourceHost h;
	h.setLimits(20.0, 0, 12u * 1024u * 1024u);
	std::string err;
	CHECK_TRUE(h.load(kHtmlMemory, "html-memory", err));
	MangaList list;
	// the documents are kept, so their charged trees pile up until the next parse would cross the cap
	CHECK_TRUE(!h.popular(1, list, err));
	CHECK_TRUE(err.find("memory") != std::string::npos);
}

int main()
{
	testCalls();
	testOptionalAndRefusals();
	testSandbox();
	testLimits();
	testPrefs();
	testJson();
	testUrl();
	testDate();
	testFilterLimits();
	testHtml();
	testHtmlMemory();
	if (failures) fprintf(stderr, "%d failed\n", failures);
	else printf("manga: all passed\n");
	return failures != 0;
}
