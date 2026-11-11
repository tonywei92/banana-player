// A source's own filters (docs/design/manga.md 5.2.1): the definitions SourceHost::filters() normalizes, the values
// the sheet keeps, and the JSON search() gets. Window-free, so the sheet's choices can be tested without a panel
// (tests/core_test.cpp).
#pragma once
#include <string>
#include <vector>

namespace bp
{
	struct MangaFilterOption
	{
		std::string id, name;
	};

	// One filter definition. `type` is one of select, sort, check, multi, tri, text or group; a group holds its
	// children in `children` and has no options of its own.
	struct MangaFilter
	{
		std::string id, type, name;
		std::vector<MangaFilterOption> options;
		std::vector<MangaFilter> children;
		// The script's `default`, where it gave one: a select's option id, a text's string, a check's bool, a sort's
		// direction, and the ids a multi/tri starts with.
		std::string textDefault;
		bool checkDefault = false;
		bool sortHasDefault = false;
		std::string sortBy;
		bool sortAsc = false;
		std::vector<std::string> multiDefault, includeDefault, excludeDefault;
		// the filter's place in its source's own list (headers and separators counted), where that differs from
		// its place here; -1: the same
		int position = -1;
	};

	// The chosen values, in the same shape as the definitions. select/sort keep an option id (not an index, so a
	// list the source reordered keeps its meaning); multi and tri have one entry per option.
	struct MangaFilterValue
	{
		std::string select, sortBy, text;
		bool sortAsc = false, check = false;
		std::vector<int> multi;	// 0 off, 1 on
		std::vector<int> tri;	// 0 off, 1 include, 2 exclude
		std::vector<MangaFilterValue> children;	// group
	};

	// The normalized filters() JSON (what SourceHost::filters gives the worker). A malformed list is an error; an
	// item with an unknown type, no id or no type is skipped, as the host already does.
	bool mangaFiltersParse(const std::string& json, std::vector<MangaFilter>& out, std::string& error);

	// Each filter's declared default, or its first option / off / empty when it declared none.
	std::vector<MangaFilterValue> mangaFilterDefaults(const std::vector<MangaFilter>& defs);

	// Makes values match defs after a source's list arrived or changed: an option the source no longer lists falls
	// back to the filter's default, and a multi/tri list is resized. Values that still fit are kept.
	void mangaFilterFit(const std::vector<MangaFilter>& defs, std::vector<MangaFilterValue>& values);

	// The object search() gets as its filters argument (5.2.1): select -> id, sort -> {by, asc}, check -> bool,
	// multi -> ids, tri -> {include, exclude}, text -> its trimmed string, group -> its children's object.
	std::string mangaFilterValuesJson(const std::vector<MangaFilter>& defs, const std::vector<MangaFilterValue>& values);

	// A row's value: the option's name, "On"/"Off", "✓2 ✗1" for a tri (or a group's children), "None" when nothing
	// is ticked, and "—" for an empty text.
	std::string mangaFilterSummary(const MangaFilter& def, const MangaFilterValue& value);
}
