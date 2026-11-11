// The source-script host: one Lua 5.4 state per user-added source, the calls of docs/design/manga.md 5.2 and the
// limits of 5.4. Window-free, no networking of its own (bp.http goes through an injected callback) and not
// thread-safe: the manga thread owns it and makes one call at a time.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bp
{
	// What bp.http.get/post hands back. The host never opens a socket; the fetcher does, and tests inject a fake.
	struct HttpResult
	{
		int status = 0;
		std::string body, url;	// url: the final one after redirects
		std::map<std::string, std::string> headers;
	};

	// method is "GET" or "POST"; timeout is in seconds. A callback may throw; the host turns that into a Lua error.
	using HttpFn = std::function<HttpResult(const std::string& method, const std::string& url,
		const std::map<std::string, std::string>& headers, const std::string& body, int timeoutSeconds)>;

	struct MangaItem
	{
		std::string key, title, cover;
		// The item's table as the script returned it, as JSON, so a later details()/chapters() gets exactly those fields
		// back (5.2). Empty when the table couldn't be copied.
		std::string json;
	};

	struct MangaList
	{
		std::vector<MangaItem> items;
		bool more = false;
	};

	struct MangaDetails
	{
		std::string title, author, artist, description, status, cover;
		std::vector<std::string> genres;
	};

	struct MangaChapter
	{
		std::string key, name, group;
		double number = 0;
		int64_t date = 0;	// seconds since the epoch; 0 when the script gave none
		// The chapter's table as the script returned it, for passing to pages() later (5.2).
		std::string json;
	};

	struct MangaPage
	{
		std::string url, key;	// one of the two: a direct address, or something image() resolves
		// The page's table as the script returned it, for passing to image() later.
		std::string json;
	};

	struct MangaImage
	{
		std::string url;
		std::map<std::string, std::string> headers;	// the source's headers plus the page's
	};

	// One entry of the script's `prefs` table, for the settings form.
	struct SourcePref
	{
		std::string id, type, name, def;
		std::vector<std::string> options;
	};

	struct SourceMeta
	{
		std::string id, name, lang, version, baseUrl;
		bool nsfw = false;
		std::map<std::string, std::string> headers;
		std::vector<SourcePref> prefs;
	};

	class SourceHost
	{
	public:
		SourceHost();
		~SourceHost();
		SourceHost(const SourceHost&) = delete;
		SourceHost& operator=(const SourceHost&) = delete;

		void setHttp(HttpFn fn);
		// Where bp.prefs persists (a JSON file). Reading happens at the next load; each set writes temp + rename.
		void setPrefsPath(std::string path);
		// Wall clock seconds per call, Lua instructions per call and the state's memory ceiling. Values <= 0 keep
		// the current one. Defaults: 20 s, 200 M instructions, 64 MB.
		void setLimits(double wallSeconds, uint64_t instructions, size_t memoryBytes);

		// A fresh state, the script loaded as text only, and the 5.2/5.3 checks: api 1, matching id, a name and
		// the required functions. `expectedId` is the repository's id for it.
		bool load(const std::string& scriptText, const std::string& expectedId, std::string& error);

		bool popular(int page, MangaList& out, std::string& error);
		bool latest(int page, MangaList& out, std::string& error);
		bool search(const std::string& query, int page, const std::string& filtersJson, MangaList& out,
			std::string& error);
		// The source's own filters as normalized JSON text (5.2.1): known types only, <= 50 filters, <= 2,000
		// options, labels clipped to 60 characters. A source without filters() gives "[]".
		bool filters(std::string& json, std::string& error);

		bool details(const std::string& seriesJson, MangaDetails& out, std::string& error);
		bool chapters(const std::string& seriesJson, std::vector<MangaChapter>& out, std::string& error);
		bool pages(const std::string& chapterJson, std::vector<MangaPage>& out, std::string& error);
		bool image(const std::string& pageJson, MangaImage& out, std::string& error);

		const SourceMeta& meta() const;
		bool loaded() const;

		// The implementation is public only so the Lua C callbacks in mangasource.cpp can name it; it isn't part of
		// the host's contract.
		struct Impl;

	private:
		std::unique_ptr<Impl> impl_;
	};
}
