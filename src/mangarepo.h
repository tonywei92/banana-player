// Manga source repositories (design 5.3): the JSON index behind a repository URL, what makes a listed source
// installable, checksums and versions, and resolving a source's files against the index URL. All pure: fetching,
// storing and the sandbox checks are the caller's (design 5.4), this only reads what was fetched.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
// compareVersions comes from update.h rather than being written twice: the app's own updater and a repository's
// sources must agree on what "1.10 after 1.9" means, in one place.
#include "update.h"

namespace bp
{
	// One source as an index lists it. `url` and `icon` are as the index writes them, relative to the index URL
	// (design 5.3); repoResolve makes them absolute. `minApp` is the script's `api` contract version kept as text
	// so compareVersions can weigh it ("needs a newer Banana Player"). `sha256` is the script file's digest.
	struct RepoSource
	{
		std::string id, name, version, lang, url, sha256, minApp;
		bool adult = false;	// the index's `nsfw`
		std::string icon;
	};

	// A repository's index: the design's format (5.3), `formatVersion` from its `format`.
	struct RepoIndex
	{
		std::string name, homepage;
		int formatVersion = 1;
		std::vector<RepoSource> sources;
	};

	// Parse and validate an index. Refused outright: more than 1 MB of text, JSON that doesn't read, a top level
	// that isn't an index, no `sources` list or more than 2000 entries, a `format` newer than this build reads (1).
	// A source failing validation (id [a-z0-9._-]{1,64}, dotted-number version, 64-hex sha256, a file that is
	// relative or an absolute https:// URL — the design's repositories are HTTPS, no private-address exception, and
	// lang a short code or "all") is skipped and named in `error`, the rest kept. true: `out` is the index with the
	// sources that passed, which can be none; then `error` names every skip.
	bool parseRepoIndex(const std::string& json, RepoIndex& out, std::string& error);

	// `href` against the index URL, as a browser resolves it: absolute stays, "/x" runs from the host's root,
	// anything else from the index's folder, with "." and ".." folded (never past the root).
	std::string repoResolve(const std::string& indexUrl, const std::string& href);

	// SHA-256 over bytes (OpenSSL EVP), lowercase hex; "" if the digest fails. The buffer overload because scripts
	// and icons arrive as bytes; update.h's string version stays for the updater's own files.
	std::string sha256Hex(const uint8_t* data, size_t n);
	// the digest matches, hex compared case-insensitively (indexes are hand-written and their case drifts)
	bool sha256Matches(const std::string& expectedHex, const uint8_t* data, size_t n);

	// the index lists a higher version than what is installed: offer *Update* (design 5.3 — never automatic)
	bool updateAvailable(const std::string& installed, const std::string& indexed);

	// A shape check for URLs this code reads or accepts: http(s) with a host, no credentials (CLAUDE.md: never),
	// no spaces or control characters, at most 2048 characters. parseRepoIndex layers the design's https-only rule
	// on top for a repository's own index; this one answers "is this a URL at all".
	bool validRepoUrl(const std::string& url);
}
