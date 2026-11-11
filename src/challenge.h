#pragma once
// What an HTTP answer says when a site stands behind a browser check (docs/design/manga.md section 10's backlog):
// a source script and the reader's own page requests can't pass one, so they should say that instead of failing
// vaguely. Pure (no socket, no curl), shared by the Lua worker and the reader's fetcher.
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bp
{
	// An answer's headers, name and value, in the order they came (the shape MangaFetch and ReaderFetch already use).
	using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

	enum class BrowserCheck
	{
		None,
		Cloudflare,
		DdosGuard,
	};

	// The check an answer is behind, or None for an ordinary answer. bodyStart is the first few KB of the body (the
	// challenge page's markers sit near its top); header names and values are matched without case.
	BrowserCheck detectBrowserCheck(int status, const HttpHeaders& headers, std::string_view bodyStart);

	// The one line the user sees for a check ("" for None), so the worker and the reader spell it the same way.
	const char* browserCheckMessage(BrowserCheck check);
}
