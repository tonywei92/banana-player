// Suwayomi-Server's GraphQL API (docs/design/manga.md, section 2): a request builder for every step the manga
// reader takes through a Java extension, and an answer parser that turns each JSON response into plain structs
// the Ui can show. Pure: no sockets, no threads, no SDL; the caller POSTs the request body to /api/graphql
// and hands the response here, as mangaremote does for Komga and OPDS.
//
// Source ids stay strings everywhere: Suwayomi writes them as 64-bit numbers that overflow a double, so parsing
// them as a number loses precision. Manga and chapter ids are ints (small, the server's own row keys).
//
// One GraphQL error shape: {"errors":[{"message":…}]} — every parser reports it in `error`, and false means
// the body wasn't usable (a crash would be worse; a bad answer is a message, not a throw).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "mangafilters.h"

namespace bp
{
	// ---- request bodies (each is the JSON to POST to /api/graphql) ----

	// the two store mutations; the index url is the .pb file the server indexes
	std::string suwayomiAddExtensionStore(const std::string& indexUrl);
	std::string suwayomiRemoveExtensionStore(const std::string& indexUrl);

	// every extension the server sees (installed or not, from every registered store)
	std::string suwayomiFetchExtensions();

	// install / update / uninstall one extension by its package name; uninstall is updateExtension with install=false
	std::string suwayomiInstallExtension(const std::string& pkgName);
	std::string suwayomiUpdateExtension(const std::string& pkgName);
	std::string suwayomiUninstallExtension(const std::string& pkgName);

	// a source's filters (the aliased query used for filters-mangaplus.json); sourceId is a string (see the header)
	std::string suwayomiFetchSourceFilters(const std::string& sourceId);

	// a source's catalogue: POPULAR, LATEST or SEARCH (query, page, filters). `filters` are the Suwayomi input
	// list the sheet built through suwayomiFilterInputs()
	std::string suwayomiFetchSourceMangaPopular(const std::string& sourceId, int page);
	std::string suwayomiFetchSourceMangaLatest(const std::string& sourceId, int page);
	std::string suwayomiFetchSourceMangaSearch(const std::string& sourceId, const std::string& query, int page,
	                                           const std::string& filterInputsJson);

	// a manga's details + its chapters
	std::string suwayomiFetchMangaAndChapters(int mangaId);

	// a chapter's page paths
	std::string suwayomiFetchChapterPages(int chapterId);

	// ---- answer types ----

	struct SuwayomiExt
	{
		std::string pkgName, name, lang, versionName, iconUrl;
		int64_t versionCode = 0;
		bool isNsfw = false, isInstalled = false, hasUpdate = false, isObsolete = false;
	};

	// the sources an install / update returns; the id is a string because Suwayomi writes 64-bit ints that
	// overflow a double (1998944621602463790 in install.json)
	struct SuwayomiSource
	{
		std::string id, name, lang, displayName;
		bool supportsLatest = false, isNsfw = false;
	};

	struct SuwayomiManga
	{
		int id = 0;
		std::string title, thumbnailUrl;
	};

	struct SuwayomiChapter
	{
		int id = 0;
		std::string name, scanlator;
		double chapterNumber = -1;
		int sourceOrder = 0, pageCount = -1, lastPageRead = 0;
		int64_t uploadDate = 0;	// milliseconds since the epoch, as Suwayomi writes them
		bool isRead = false;
	};

	// a manga's own details (the "series" page's header); `genre` and `status` are the server's own text
	struct SuwayomiMangaDetails
	{
		int id = 0;
		std::string title, author, artist, description, status, thumbnailUrl, realUrl;
		std::vector<std::string> genre;
	};

	// one entry in the filter input list the sheet turns back into a Suwayomi request. Each one carries the
	// filter's position in the source's list and exactly one state field; the others stay at their defaults
	struct SuwayomiFilterInput
	{
		int position = 0;
		bool hasSelect = false, hasSort = false, hasCheck = false, hasTri = false, hasText = false, hasGroup = false;
		int selectState = 0;
		int sortIndex = 0;
		bool sortAscending = false;
		bool checkBoxState = false;
		int triState = 0;	// 0 off, 1 include, 2 exclude
		std::string textState;
		std::string groupChangeJson;	// the nested FilterChangeInput list, already JSON-encoded
	};

	// ---- parsers ----

	// every parser fills `error` with the GraphQL error when the body has one, and returns false for a body that
	// isn't the expected shape. `out` is unchanged on failure.

	// the GraphQL error check: true when the body has {"errors":[{"message":…}]}, filling `error` with the
	// first message. false (and error empty) when the body has no errors at that path
	bool suwayomiCheckErrors(const std::string& json, std::string& error);

	bool suwayomiParseAddExtensionStore(const std::string& json, std::string& error);
	bool suwayomiParseRemoveExtensionStore(const std::string& json, std::string& error);

	bool suwayomiParseExtensions(const std::string& json, std::vector<SuwayomiExt>& out, std::string& error);

	// the install / update / uninstall answers: the extension's sources (the sources list the install returns)
	bool suwayomiParseInstallResult(const std::string& json, std::vector<SuwayomiSource>& out, std::string& error);

	// the source's filter definitions, converted onto the MangaFilter structs the filter sheet already uses
	bool suwayomiParseFilters(const std::string& json, std::vector<MangaFilter>& out, std::string& error);

	// one page of a source's catalogue (popular, latest, search). `thumbnailUrl` is made absolute against `baseUrl`
	bool suwayomiParseSourceManga(const std::string& baseUrl, const std::string& json,
		std::vector<SuwayomiManga>& out, bool& hasNextPage, std::string& error);

	// a manga's details and its chapters, ordered for the series page (sourceOrder ascending)
	bool suwayomiParseMangaAndChapters(const std::string& baseUrl, const std::string& json,
		SuwayomiMangaDetails& manga, std::vector<SuwayomiChapter>& chapters, std::string& error);

	// a chapter's page paths, each made absolute against `baseUrl`
	bool suwayomiParsePages(const std::string& baseUrl, const std::string& json,
		std::vector<std::string>& out, std::string& error);

	// ---- filter conversion ----

	// the filter sheet's chosen values onto the Suwayomi input list the request needs. The definitions tell the
	// position each filter sits at (the source's order), and the values carry the user's choice; one entry per
	// filter, with only the field its type needs set
	std::string suwayomiFilterInputsJson(const std::vector<MangaFilter>& defs, const std::vector<MangaFilterValue>& vals);
}
