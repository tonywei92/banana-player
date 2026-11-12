// Suwayomi's GraphQL requests and answers, pure (see the header): one small JSON parser lives here because json.h
// only writes, and every GraphQL response goes through it. Nothing fetches, nothing waits, no SDL — the caller
// POSTs the request bodies and hands the response bodies here, as mangaremote does for Komga.
#include "suwayomi.h"
#include "json.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace bp
{
	namespace
	{
		// ---- the JSON reader (same shape as mangafilters.cpp's and opds.cpp's, small and self-contained) ----

		const size_t kMaxInput = 8u << 20;
		const size_t kMaxDepth = 64;

		struct JVal
		{
			enum Kind { NUL, BOOL, NUM, STR, ARR, OBJ } kind = NUL;
			bool boolean = false;
			double num = 0;
			std::string s;
			std::vector<std::pair<std::string, JVal>> members;
			std::vector<JVal> elems;

			const JVal* find(const std::string& key) const
			{
				for (const auto& m : members)
					if (m.first == key) return &m.second;
				return nullptr;
			}
		};

		void jWs(const std::string& s, size_t& p)
		{
			while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) p++;
		}

		bool jHex4(const std::string& s, size_t p, unsigned& cp)
		{
			if (p + 4 > s.size()) return false;
			cp = 0;
			for (size_t i = 0; i < 4; i++)
			{
				char c = s[p + i];
				unsigned v;
				if (c >= '0' && c <= '9') v = c - '0';
				else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
				else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
				else return false;
				cp = cp * 16 + v;
			}
			return true;
		}

		void appendUtf8(std::string& out, unsigned cp)
		{
			if (cp < 0x80) out += (char)cp;
			else if (cp < 0x800)
			{
				out += (char)(0xC0 | (cp >> 6));
				out += (char)(0x80 | (cp & 0x3F));
			}
			else if (cp < 0x10000)
			{
				out += (char)(0xE0 | (cp >> 12));
				out += (char)(0x80 | ((cp >> 6) & 0x3F));
				out += (char)(0x80 | (cp & 0x3F));
			}
			else
			{
				out += (char)(0xF0 | (cp >> 18));
				out += (char)(0x80 | ((cp >> 12) & 0x3F));
				out += (char)(0x80 | ((cp >> 6) & 0x3F));
				out += (char)(0x80 | (cp & 0x3F));
			}
		}

		bool jString(const std::string& s, size_t& p, std::string& out)
		{
			if (p >= s.size() || s[p] != '"') return false;
			p++;
			out.clear();
			while (p < s.size())
			{
				char c = s[p++];
				if (c == '"') return true;
				if (c != '\\') { out += c; continue; }
				if (p >= s.size()) return false;
				char e = s[p++];
				switch (e)
				{
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u':
				{
					unsigned cp;
					if (!jHex4(s, p, cp)) return false;
					p += 4;
					if (cp >= 0xD800 && cp <= 0xDBFF)
					{
						if (p + 6 > s.size() || s[p] != '\\' || s[p + 1] != 'u') return false;
						unsigned lo;
						if (!jHex4(s, p + 2, lo)) return false;
						if (lo < 0xDC00 || lo > 0xDFFF) return false;
						p += 6;
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					}
					else if (cp >= 0xD800 && cp <= 0xDFFF) return false;
					appendUtf8(out, cp);
					break;
				}
				default: return false;
				}
			}
			return false;
		}

		bool jNumber(const std::string& s, size_t& p, JVal& v)
		{
			size_t start = p;
			if (p < s.size() && s[p] == '-') p++;
			while (p < s.size() && std::isdigit((unsigned char)s[p])) p++;
			if (p < s.size() && s[p] == '.')
			{
				p++;
				while (p < s.size() && std::isdigit((unsigned char)s[p])) p++;
			}
			if (p < s.size() && (s[p] == 'e' || s[p] == 'E'))
			{
				p++;
				if (p < s.size() && (s[p] == '+' || s[p] == '-')) p++;
				while (p < s.size() && std::isdigit((unsigned char)s[p])) p++;
			}
			if (p == start) return false;
			v.kind = JVal::NUM;
			v.s = s.substr(start, p - start);
			v.num = std::strtod(v.s.c_str(), nullptr);
			return true;
		}

		bool jValue(const std::string& s, size_t& p, size_t depth, JVal& v);

		bool jObject(const std::string& s, size_t& p, size_t depth, JVal& v)
		{
			v.kind = JVal::OBJ;
			p++;	// the opening {
			jWs(s, p);
			if (p < s.size() && s[p] == '}') { p++; return true; }
			while (true)
			{
				jWs(s, p);
				if (p >= s.size() || s[p] != '"') return false;
				std::string key;
				if (!jString(s, p, key)) return false;
				jWs(s, p);
				if (p >= s.size() || s[p] != ':') return false;
				p++;
				JVal child;
				if (!jValue(s, p, depth + 1, child)) return false;
				v.members.emplace_back(std::move(key), std::move(child));
				jWs(s, p);
				if (p >= s.size()) return false;
				if (s[p] == '}') { p++; return true; }
				if (s[p] != ',') return false;
				p++;
			}
		}

		bool jArray(const std::string& s, size_t& p, size_t depth, JVal& v)
		{
			v.kind = JVal::ARR;
			p++;	// the opening [
			jWs(s, p);
			if (p < s.size() && s[p] == ']') { p++; return true; }
			while (true)
			{
				JVal child;
				if (!jValue(s, p, depth + 1, child)) return false;
				v.elems.push_back(std::move(child));
				jWs(s, p);
				if (p >= s.size()) return false;
				if (s[p] == ']') { p++; return true; }
				if (s[p] != ',') return false;
				p++;
			}
		}

		bool jValue(const std::string& s, size_t& p, size_t depth, JVal& v)
		{
			if (depth > kMaxDepth) return false;
			jWs(s, p);
			if (p >= s.size()) return false;
			char c = s[p];
			if (c == '{') return jObject(s, p, depth, v);
			if (c == '[') return jArray(s, p, depth, v);
			if (c == '"')
			{
				v.kind = JVal::STR;
				return jString(s, p, v.s);
			}
			if (s.compare(p, 4, "true") == 0) { p += 4; v.kind = JVal::BOOL; v.boolean = true; return true; }
			if (s.compare(p, 5, "false") == 0) { p += 5; v.kind = JVal::BOOL; v.boolean = false; return true; }
			if (s.compare(p, 4, "null") == 0) { p += 4; v.kind = JVal::NUL; return true; }
			if (c == '-' || std::isdigit((unsigned char)c)) return jNumber(s, p, v);
			return false;
		}

		bool jParse(const std::string& s, JVal& v)
		{
			if (s.size() > kMaxInput) return false;
			size_t p = 0;
			if (!jValue(s, p, 0, v)) return false;
			jWs(s, p);
			return p == s.size();
		}

		// a string read from the parsed tree: "" when the field isn't there or isn't a string
		std::string jStr(const JVal* v)
		{
			return v && v->kind == JVal::STR ? v->s : std::string();
		}

		int jInt(const JVal* v, int fallback = 0)
		{
			return v && v->kind == JVal::NUM ? (int)v->num : fallback;
		}

		int64_t jInt64(const JVal* v, int64_t fallback = 0)
		{
			return v && v->kind == JVal::NUM ? (int64_t)v->num : fallback;
		}

		double jDouble(const JVal* v, double fallback = -1)
		{
			return v && v->kind == JVal::NUM ? v->num : fallback;
		}

		bool jBool(const JVal* v, bool fallback = false)
		{
			return v && v->kind == JVal::BOOL ? v->boolean : fallback;
		}

		// base + path joined with exactly one slash: the thumbnail and page urls Suwayomi returns are
		// root-relative ("/api/v1/manga/1/thumbnail") and need the server's base in front of them
		std::string joinUrl(const std::string& base, const std::string& path)
		{
			if (path.empty()) return base;
			if (base.empty()) return path;
			// already absolute: leave it
			if (path.compare(0, 7, "http://") == 0 || path.compare(0, 8, "https://") == 0) return path;
			std::string b = base;
			while (!b.empty() && b.back() == '/') b.pop_back();
			std::string p = path;
			if (!p.empty() && p[0] != '/') p = "/" + p;
			return b + p;
		}

		// ---- the GraphQL request builders ----

		// a GraphQL body with a query and, where present, variables; variables are put in raw so the caller's
		// already-encoded JSON flows through without being escaped a second time
		std::string gqlBody(const std::string& query, const std::string& variablesJson = "")
		{
			JsonWriter w;
			w.obj();
			w.key("query").str(query);
			if (!variablesJson.empty())
			{
				w.key("variables").raw(variablesJson);
			}
			w.end();
			return w.out();
		}

		// the id of a source has to be sent as a string, because Suwayomi's own ids overflow a double and would
		// lose precision on the wire if they were written as numbers
		std::string sourceVar(const std::string& sourceId)
		{
			JsonWriter w;
			w.obj();
			w.key("source").str(sourceId);
			w.end();
			return w.out();
		}

		// ---- filter conversion helpers ----

		// a select's chosen option id onto the index the source's values array uses
		int selectIndex(const MangaFilter& def, const std::string& optionId)
		{
			for (size_t i = 0; i < def.options.size(); i++)
				if (def.options[i].id == optionId) return (int)i;
			return 0;	// unknown id: the first option, as the default
		}

		// a tri state value onto Suwayomi's own int (0 off, 1 include, 2 exclude)
		int triToSuwayomi(int ourTri)
		{
			// our tri is the same encoding (mangafilters.h: 0 off, 1 include, 2 exclude)
			return ourTri < 0 || ourTri > 2 ? 0 : ourTri;
		}

		// One value-carrying filter and the positions that reach it: the top-level index, then one index per
		// group crossed. A leaf inside two groups is {g1, g2, leaf}: Suwayomi takes that as
		// {position:g1, groupChange:{position:g2, groupChange:{position:leaf, ...}}}.
		struct FilterLeaf
		{
			std::vector<int> path;
			const MangaFilter* def = nullptr;
			const MangaFilterValue* val = nullptr;
		};

		void collectFilterLeaves(const std::vector<MangaFilter>& defs, const std::vector<MangaFilterValue>& vals,
			std::vector<int>& path, std::vector<FilterLeaf>& out)
		{
			for (size_t i = 0; i < defs.size() && i < vals.size(); i++)
			{
				const MangaFilter& d = defs[i];
				const MangaFilterValue& v = vals[i];
				int pos = d.position >= 0 ? d.position : (int)i;	// the source's own place, headers counted
				if (d.type == "group")
				{
					path.push_back(pos);
					collectFilterLeaves(d.children, v.children, path, out);
					path.pop_back();
					continue;
				}
				path.push_back(pos);
				FilterLeaf leaf;
				leaf.path = path;
				leaf.def = &d;
				leaf.val = &v;
				out.push_back(std::move(leaf));
				path.pop_back();
			}
		}

		void writeFilterState(JsonWriter& w, const MangaFilter& d, const MangaFilterValue& v)
		{
			if (d.type == "select")
			{
				w.key("selectState").num(selectIndex(d, v.select));
			}
			else if (d.type == "sort")
			{
				int idx = selectIndex(d, v.sortBy);
				w.key("sortState").obj().key("index").num(idx).key("ascending").boolean(v.sortAsc).end();
			}
			else if (d.type == "check")
			{
				w.key("checkBoxState").boolean(v.check);
			}
			else if (d.type == "tri")
			{
				// a tri state filter has no option list; the sheet's single tri value maps straight
				// onto Suwayomi's triState, an enum by name (a number is refused: "No value found for name '0'")
				int tri = v.tri.empty() ? 0 : triToSuwayomi(v.tri[0]);
				w.key("triState").str(tri == 1 ? "INCLUDE" : tri == 2 ? "EXCLUDE" : "IGNORE");
			}
			else if (d.type == "text")
			{
				// Suwayomi's textState is the trimmed string; the sheet keeps the raw input, so trim here
				std::string t = v.text;
				size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
				t = a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
				w.key("textState").str(t);
			}
		}
	}

	// ---- request builders ----

	std::string suwayomiAddExtensionStore(const std::string& indexUrl)
	{
		const char* q = "mutation AddExtensionStore($indexUrl: String!) { addExtensionStore(input: {indexUrl: $indexUrl}) { extensionStore { name indexUrl } } }";
		JsonWriter v;
		v.obj().key("indexUrl").str(indexUrl).end();
		return gqlBody(q, v.out());
	}

	std::string suwayomiRemoveExtensionStore(const std::string& indexUrl)
	{
		const char* q = "mutation RemoveExtensionStore($indexUrl: String!) { removeExtensionStore(input: {indexUrl: $indexUrl}) { extensionStore { indexUrl } } }";
		JsonWriter v;
		v.obj().key("indexUrl").str(indexUrl).end();
		return gqlBody(q, v.out());
	}

	std::string suwayomiFetchExtensions()
	{
		const char* q = "mutation { fetchExtensions(input: {}) { extensions { pkgName name lang versionName versionCode isNsfw isInstalled hasUpdate isObsolete iconUrl } } }";
		return gqlBody(q);
	}

	std::string suwayomiInstallExtension(const std::string& pkgName)
	{
		const char* q = "mutation InstallExtension($pkgName: String!) { updateExtension(input: {id: $pkgName, patch: {install: true}}) { extension { pkgName isInstalled source { nodes { id name lang displayName supportsLatest isNsfw } } } } }";
		JsonWriter v;
		v.obj().key("pkgName").str(pkgName).end();
		return gqlBody(q, v.out());
	}

	std::string suwayomiUpdateExtension(const std::string& pkgName)
	{
		const char* q = "mutation UpdateExtension($pkgName: String!) { updateExtension(input: {id: $pkgName, patch: {update: true}}) { extension { pkgName isInstalled source { nodes { id name lang displayName supportsLatest isNsfw } } } } }";
		JsonWriter v;
		v.obj().key("pkgName").str(pkgName).end();
		return gqlBody(q, v.out());
	}

	std::string suwayomiUninstallExtension(const std::string& pkgName)
	{
		const char* q = "mutation UninstallExtension($pkgName: String!) { updateExtension(input: {id: $pkgName, patch: {uninstall: true}}) { extension { pkgName isInstalled source { nodes { id name lang displayName supportsLatest isNsfw } } } } }";
		JsonWriter v;
		v.obj().key("pkgName").str(pkgName).end();
		return gqlBody(q, v.out());
	}

	std::string suwayomiFetchSourceFilters(const std::string& sourceId)
	{
		// the aliased query: we ask for a `source` with the id we know, and read its `filters` — the same
		// shape filters-mangaplus.json has
		const char* q = "query SourceFilters($source: LongString!) { source(id: $source) { id name supportsLatest filters { __typename ... on SelectFilter { name values selectDefault: default } ... on SortFilter { name values sortDefault: default { index ascending } } ... on CheckBoxFilter { name checkBoxDefault: default } ... on TriStateFilter { name triDefault: default } ... on TextFilter { name textDefault: default } ... on HeaderFilter { name } ... on SeparatorFilter { name } ... on GroupFilter { name filters { __typename ... on SelectFilter { name values selectDefault: default } ... on SortFilter { name values sortDefault: default { index ascending } } ... on CheckBoxFilter { name checkBoxDefault: default } ... on TriStateFilter { name triDefault: default } ... on TextFilter { name textDefault: default } ... on HeaderFilter { name } ... on SeparatorFilter { name } } } } } }";
		return gqlBody(q, sourceVar(sourceId));
	}

	std::string suwayomiFetchSourceMangaPopular(const std::string& sourceId, int page)
	{
		const char* q = "mutation Popular($source: LongString!, $page: Int!) { fetchSourceManga(input: {source: $source, type: POPULAR, page: $page}) { hasNextPage mangas { id title thumbnailUrl } } }";
		JsonWriter v;
		v.obj().key("source").str(sourceId).key("page").num(page).end();
		return gqlBody(q, v.out());
	}

	std::string suwayomiFetchSourceMangaLatest(const std::string& sourceId, int page)
	{
		const char* q = "mutation Latest($source: LongString!, $page: Int!) { fetchSourceManga(input: {source: $source, type: LATEST, page: $page}) { hasNextPage mangas { id title thumbnailUrl } } }";
		JsonWriter v;
		v.obj().key("source").str(sourceId).key("page").num(page).end();
		return gqlBody(q, v.out());
	}

	std::string suwayomiFetchSourceMangaSearch(const std::string& sourceId, const std::string& query, int page,
	                                           const std::string& filterInputsJson)
	{
		const char* q = "mutation Search($source: LongString!, $query: String, $page: Int!, $filters: [FilterChangeInput!]) { fetchSourceManga(input: {source: $source, type: SEARCH, query: $query, page: $page, filters: $filters}) { hasNextPage mangas { id title thumbnailUrl } } }";
		JsonWriter v;
		v.obj();
		v.key("source").str(sourceId);
		v.key("query").str(query);
		v.key("page").num(page);
		// the filters arrive already encoded; they are a JSON array the query takes as-is
		v.key("filters").raw(filterInputsJson.empty() ? "[]" : filterInputsJson);
		v.end();
		return gqlBody(q, v.out());
	}

	std::string suwayomiFetchMangaAndChapters(int mangaId)
	{
		const char* q = "mutation MangaAndChapters($id: Int!) { fetchMangaAndChapters(input: {id: $id, fetchManga: true, fetchChapters: true}) { manga { id title author artist description status realUrl thumbnailUrl genre } chapters { id name chapterNumber sourceOrder uploadDate scanlator pageCount isRead lastPageRead } } }";
		JsonWriter v;
		v.obj().key("id").num(mangaId).end();
		return gqlBody(q, v.out());
	}

	std::string suwayomiCheckManga(int mangaId)
	{
		const char* q = "query Check($id: Int!) { manga(id: $id) { id title sourceId chapters { nodes { id } } } }";
		JsonWriter v;
		v.obj().key("id").num(mangaId).end();
		return gqlBody(q, v.out());
	}

	std::string suwayomiFetchChapterPages(int chapterId)
	{
		const char* q = "mutation ChapterPages($id: Int!) { fetchChapterPages(input: {chapterId: $id}) { pages chapter { id pageCount } } }";
		JsonWriter v;
		v.obj().key("id").num(chapterId).end();
		return gqlBody(q, v.out());
	}

	// ---- parsers ----

	bool suwayomiCheckErrors(const std::string& json, std::string& error)
	{
		error.clear();
		JVal root;
		if (!jParse(json, root)) return false;
		if (root.kind != JVal::OBJ) return false;
		const JVal* errors = root.find("errors");
		if (!errors || errors->kind != JVal::ARR || errors->elems.empty()) return false;
		const JVal& first = errors->elems[0];
		if (first.kind != JVal::OBJ) return false;
		const JVal* msg = first.find("message");
		error = jStr(msg);
		if (error.empty()) error = "GraphQL error";
		return true;
	}

	bool suwayomiParseAddExtensionStore(const std::string& json, std::string& error, std::string* name)
	{
		error.clear();
		if (suwayomiCheckErrors(json, error)) return false;
		JVal root;
		if (!jParse(json, root)) { error = "bad JSON"; return false; }
		const JVal* data = root.find("data");
		if (!data || data->kind != JVal::OBJ) { error = "no data"; return false; }
		const JVal* add = data->find("addExtensionStore");
		if (!add || add->kind != JVal::OBJ) { error = "no addExtensionStore"; return false; }
		const JVal* store = add->find("extensionStore");
		if (!store || store->kind != JVal::OBJ) { error = "no extensionStore"; return false; }
		if (name)
		{
			std::string storeName = jStr(store->find("name"));
			if (!storeName.empty()) *name = storeName;
		}
		return true;
	}

	bool suwayomiParseRemoveExtensionStore(const std::string& json, std::string& error)
	{
		error.clear();
		if (suwayomiCheckErrors(json, error)) return false;
		JVal root;
		if (!jParse(json, root)) { error = "bad JSON"; return false; }
		const JVal* data = root.find("data");
		if (!data || data->kind != JVal::OBJ) { error = "no data"; return false; }
		const JVal* rem = data->find("removeExtensionStore");
		if (!rem || rem->kind != JVal::OBJ) { error = "no removeExtensionStore"; return false; }
		return true;
	}

	bool suwayomiParseExtensions(const std::string& json, std::vector<SuwayomiExt>& out, std::string& error)
	{
		error.clear();
		if (suwayomiCheckErrors(json, error)) return false;
		JVal root;
		if (!jParse(json, root)) { error = "bad JSON"; return false; }
		const JVal* data = root.find("data");
		if (!data || data->kind != JVal::OBJ) { error = "no data"; return false; }
		const JVal* fetch = data->find("fetchExtensions");
		if (!fetch || fetch->kind != JVal::OBJ) { error = "no fetchExtensions"; return false; }
		const JVal* list = fetch->find("extensions");
		if (!list || list->kind != JVal::ARR) { error = "no extensions list"; return false; }
		out.clear();
		out.reserve(list->elems.size());
		for (const JVal& el : list->elems)
		{
			if (el.kind != JVal::OBJ) continue;
			SuwayomiExt e;
			e.pkgName = jStr(el.find("pkgName"));
			e.name = jStr(el.find("name"));
			e.lang = jStr(el.find("lang"));
			e.versionName = jStr(el.find("versionName"));
			e.iconUrl = jStr(el.find("iconUrl"));
			e.versionCode = jInt64(el.find("versionCode"));
			e.isNsfw = jBool(el.find("isNsfw"));
			e.isInstalled = jBool(el.find("isInstalled"));
			e.hasUpdate = jBool(el.find("hasUpdate"));
			e.isObsolete = jBool(el.find("isObsolete"));
			if (e.pkgName.empty()) continue;	// an entry without a key can't be addressed later
			out.push_back(std::move(e));
		}
		return true;
	}

	bool suwayomiParseInstallResult(const std::string& json, std::vector<SuwayomiSource>& out, std::string& error)
	{
		error.clear();
		if (suwayomiCheckErrors(json, error)) return false;
		JVal root;
		if (!jParse(json, root)) { error = "bad JSON"; return false; }
		const JVal* data = root.find("data");
		if (!data || data->kind != JVal::OBJ) { error = "no data"; return false; }
		const JVal* upd = data->find("updateExtension");
		if (!upd || upd->kind != JVal::OBJ) { error = "no updateExtension"; return false; }
		const JVal* ext = upd->find("extension");
		if (!ext || ext->kind != JVal::OBJ) { error = "no extension"; return false; }
		const JVal* src = ext->find("source");
		if (!src || src->kind != JVal::OBJ) { error = "no source"; return false; }
		const JVal* nodes = src->find("nodes");
		if (!nodes || nodes->kind != JVal::ARR) { error = "no source nodes"; return false; }
		out.clear();
		out.reserve(nodes->elems.size());
		for (const JVal& el : nodes->elems)
		{
			if (el.kind != JVal::OBJ) continue;
			SuwayomiSource s;
			// the source id is a string: Suwayomi writes 64-bit ints (1998944621602463790) that overflow a
			// double, so it must be read as the string it is, not parsed as a number
			const JVal* id = el.find("id");
			if (id && id->kind == JVal::STR) s.id = id->s;
			else if (id && id->kind == JVal::NUM) s.id = id->s;	// the raw token the parser kept
			s.name = jStr(el.find("name"));
			s.lang = jStr(el.find("lang"));
			s.displayName = jStr(el.find("displayName"));
			s.supportsLatest = jBool(el.find("supportsLatest"));
			s.isNsfw = jBool(el.find("isNsfw"));
			if (s.id.empty()) continue;
			out.push_back(std::move(s));
		}
		return true;
	}

	// a Suwayomi filter onto a MangaFilter. The SelectFilter carries its options as a `values` array of
	// strings; our ids are the values themselves (the sheet matches by id, and Suwayomi uses the index of
	// the value in the array to talk about a choice).
	static bool convertSuwayomiFilter(const JVal& f, MangaFilter& out)
	{
		const JVal* tn = f.find("__typename");
		std::string type = jStr(tn);
		out.name = jStr(f.find("name"));
		if (out.name.empty()) out.name = type;	// a nameless filter shows its type, never nothing
		// the id is the name: Suwayomi addresses filters by position, so our id is just a label, and the
		// position is the filter's index in the source's list (see suwayomiFilterInputsJson)
		out.id = out.name;
		if (type == "SelectFilter")
		{
			out.type = "select";
			const JVal* vals = f.find("values");
			if (vals && vals->kind == JVal::ARR)
			{
				for (const JVal& v : vals->elems)
				{
					if (v.kind != JVal::STR || v.s.empty()) continue;
					MangaFilterOption o;
					o.id = v.s;	// the option's value is its id; the position is its index
					o.name = v.s;
					out.options.push_back(std::move(o));
				}
			}
			// selectDefault is an index; our textDefault is the id (the value string at that index)
			const JVal* def = f.find("selectDefault");
			int idx = jInt(def, 0);
			if (idx >= 0 && idx < (int)out.options.size()) out.textDefault = out.options[idx].id;
		}
		else if (type == "SortFilter")
		{
			out.type = "sort";
			const JVal* vals = f.find("values");
			if (vals && vals->kind == JVal::ARR)
			{
				for (const JVal& v : vals->elems)
				{
					if (v.kind != JVal::STR || v.s.empty()) continue;
					MangaFilterOption o;
					o.id = v.s;
					o.name = v.s;
					out.options.push_back(std::move(o));
				}
			}
			const JVal* def = f.find("sortDefault");
			if (def && def->kind == JVal::OBJ)
			{
				int idx = jInt(def->find("index"), 0);
				if (idx >= 0 && idx < (int)out.options.size())
				{
					out.sortHasDefault = true;
					out.sortBy = out.options[idx].id;
					const JVal* asc = def->find("ascending");
					out.sortAsc = jBool(asc, false);
				}
			}
		}
		else if (type == "CheckBoxFilter")
		{
			out.type = "check";
			out.checkDefault = jBool(f.find("checkBoxDefault") ? f.find("checkBoxDefault") : f.find("checkDefault"), false);
		}
		else if (type == "TriStateFilter")
		{
			// A Suwayomi tri state is one filter that is off, include or exclude — not a list of options. The
			// sheet's tri row is a checklist, so it gets the filter itself as its single option: A then cycles
			// that row off -> include -> exclude, and suwayomiFilterInputsJson turns it back into a triState.
			out.type = "tri";
			MangaFilterOption o;
			o.id = "tri";
			o.name = out.name;
			out.options.push_back(std::move(o));
			std::string def = jStr(f.find("triDefault"));
			if (def == "INCLUDE") out.includeDefault = {out.options[0].id};
			else if (def == "EXCLUDE") out.excludeDefault = {out.options[0].id};
		}
		else if (type == "TextFilter")
		{
			out.type = "text";
			out.textDefault = jStr(f.find("textDefault"));
		}
		else if (type == "GroupFilter")
		{
			out.type = "group";
			const JVal* kids = f.find("filters");
			if (kids && kids->kind == JVal::ARR)
			{
				int at = -1;
				for (const JVal& c : kids->elems)
				{
					at++;
					if (c.kind != JVal::OBJ) continue;
					MangaFilter child;
					child.position = at;
					if (convertSuwayomiFilter(c, child)) out.children.push_back(std::move(child));
				}
			}
		}
		else if (type == "HeaderFilter" || type == "SeparatorFilter")
		{
			// headers and separators have no value; the sheet shows them by name and skips them in the
			// search() payload. Keeping them as type "text" with an empty default would still produce an
			// input entry the server doesn't want, so they are dropped by the caller
			return false;
		}
		else return false;	// an unknown filter type: not usable
		return true;
	}

	bool suwayomiParseFilters(const std::string& json, std::vector<MangaFilter>& out, std::string& error)
	{
		error.clear();
		if (suwayomiCheckErrors(json, error)) return false;
		JVal root;
		if (!jParse(json, root)) { error = "bad JSON"; return false; }
		const JVal* data = root.find("data");
		if (!data || data->kind != JVal::OBJ) { error = "no data"; return false; }
		const JVal* src = data->find("source");
		if (!src || src->kind != JVal::OBJ) { error = "no source"; return false; }
		const JVal* filters = src->find("filters");
		if (!filters || filters->kind != JVal::ARR) { error = "no filters list"; return false; }
		out.clear();
		// (the source counts its headers and separators: each filter keeps its own place for the search's positions)
		int at = -1;
		for (const JVal& f : filters->elems)
		{
			at++;
			if (f.kind != JVal::OBJ) continue;
			MangaFilter m;
			m.position = at;
			if (!convertSuwayomiFilter(f, m)) continue;	// a header or separator is dropped quietly
			out.push_back(std::move(m));
		}
		return true;
	}

	bool suwayomiParseSourceManga(const std::string& baseUrl, const std::string& json,
		std::vector<SuwayomiManga>& out, bool& hasNextPage, std::string& error)
	{
		error.clear();
		if (suwayomiCheckErrors(json, error)) return false;
		JVal root;
		if (!jParse(json, root)) { error = "bad JSON"; return false; }
		const JVal* data = root.find("data");
		if (!data || data->kind != JVal::OBJ) { error = "no data"; return false; }
		const JVal* fsm = data->find("fetchSourceManga");
		if (!fsm || fsm->kind != JVal::OBJ) { error = "no fetchSourceManga"; return false; }
		hasNextPage = jBool(fsm->find("hasNextPage"), false);
		const JVal* mangas = fsm->find("mangas");
		if (!mangas || mangas->kind != JVal::ARR) { error = "no mangas list"; return false; }
		out.clear();
		out.reserve(mangas->elems.size());
		for (const JVal& el : mangas->elems)
		{
			if (el.kind != JVal::OBJ) continue;
			SuwayomiManga m;
			m.id = jInt(el.find("id"));
			m.title = jStr(el.find("title"));
			m.thumbnailUrl = joinUrl(baseUrl, jStr(el.find("thumbnailUrl")));
			out.push_back(std::move(m));
		}
		return true;
	}

	bool suwayomiParseMangaAndChapters(const std::string& baseUrl, const std::string& json,
		SuwayomiMangaDetails& manga, std::vector<SuwayomiChapter>& chapters, std::string& error)
	{
		error.clear();
		if (suwayomiCheckErrors(json, error)) return false;
		JVal root;
		if (!jParse(json, root)) { error = "bad JSON"; return false; }
		const JVal* data = root.find("data");
		if (!data || data->kind != JVal::OBJ) { error = "no data"; return false; }
		const JVal* mc = data->find("fetchMangaAndChapters");
		if (!mc || mc->kind != JVal::OBJ) { error = "no fetchMangaAndChapters"; return false; }
		const JVal* m = mc->find("manga");
		if (!m || m->kind != JVal::OBJ) { error = "no manga"; return false; }
		manga.id = jInt(m->find("id"));
		manga.title = jStr(m->find("title"));
		manga.author = jStr(m->find("author"));
		manga.artist = jStr(m->find("artist"));
		manga.description = jStr(m->find("description"));
		manga.status = jStr(m->find("status"));
		manga.thumbnailUrl = joinUrl(baseUrl, jStr(m->find("thumbnailUrl")));
		manga.realUrl = jStr(m->find("realUrl"));
		const JVal* genre = m->find("genre");
		manga.genre.clear();
		if (genre && genre->kind == JVal::ARR)
			for (const JVal& g : genre->elems)
				if (g.kind == JVal::STR && !g.s.empty()) manga.genre.push_back(g.s);
		const JVal* chs = mc->find("chapters");
		if (!chs || chs->kind != JVal::ARR) { error = "no chapters list"; return false; }
		chapters.clear();
		chapters.reserve(chs->elems.size());
		for (const JVal& el : chs->elems)
		{
			if (el.kind != JVal::OBJ) continue;
			SuwayomiChapter c;
			c.id = jInt(el.find("id"));
			c.name = jStr(el.find("name"));
			c.scanlator = jStr(el.find("scanlator"));
			c.chapterNumber = jDouble(el.find("chapterNumber"), -1);
			c.sourceOrder = jInt(el.find("sourceOrder"));
			c.pageCount = jInt(el.find("pageCount"), -1);
			c.lastPageRead = jInt(el.find("lastPageRead"));
			// Suwayomi writes the upload date as a string of milliseconds ("1547996400000" in the fixture):
			// read it as a number when it came that way, or parse the string when it came quoted
			const JVal* ud = el.find("uploadDate");
			if (ud && ud->kind == JVal::NUM) c.uploadDate = (int64_t)ud->num;
			else if (ud && ud->kind == JVal::STR && !ud->s.empty()) c.uploadDate = std::strtoll(ud->s.c_str(), nullptr, 10);
			c.isRead = jBool(el.find("isRead"), false);
			chapters.push_back(std::move(c));
		}
		// ordered for the series page: sourceOrder ascending (the order the source published them in)
		std::sort(chapters.begin(), chapters.end(),
			[](const SuwayomiChapter& a, const SuwayomiChapter& b) { return a.sourceOrder < b.sourceOrder; });
		return true;
	}

	bool suwayomiParsePages(const std::string& baseUrl, const std::string& json,
		std::vector<std::string>& out, std::string& error)
	{
		error.clear();
		if (suwayomiCheckErrors(json, error)) return false;
		JVal root;
		if (!jParse(json, root)) { error = "bad JSON"; return false; }
		const JVal* data = root.find("data");
		if (!data || data->kind != JVal::OBJ) { error = "no data"; return false; }
		const JVal* fcp = data->find("fetchChapterPages");
		if (!fcp || fcp->kind != JVal::OBJ) { error = "no fetchChapterPages"; return false; }
		const JVal* pages = fcp->find("pages");
		if (!pages || pages->kind != JVal::ARR) { error = "no pages list"; return false; }
		out.clear();
		out.reserve(pages->elems.size());
		for (const JVal& el : pages->elems)
		{
			if (el.kind != JVal::STR) continue;
			out.push_back(joinUrl(baseUrl, el.s));
		}
		return true;
	}

	bool suwayomiParseCheckManga(const std::string& json, SuwayomiMangaCheck& out, std::string& error)
	{
		error.clear();
		out = SuwayomiMangaCheck{};
		if (suwayomiCheckErrors(json, error)) return false;
		JVal root;
		if (!jParse(json, root)) { error = "bad JSON"; return false; }
		const JVal* data = root.find("data");
		const JVal* m = data && data->kind == JVal::OBJ ? data->find("manga") : nullptr;
		if (!m || m->kind != JVal::OBJ) { error = "no manga"; return false; }
		out.title = jStr(m->find("title"));
		const JVal* src = m->find("sourceId");
		out.sourceId = src && src->kind == JVal::STR ? src->s : std::to_string(jInt64(src));
		const JVal* ch = m->find("chapters");
		const JVal* nodes = ch && ch->kind == JVal::OBJ ? ch->find("nodes") : nullptr;
		if (nodes && nodes->kind == JVal::ARR)
			for (const JVal& n : nodes->elems)
				if (n.kind == JVal::OBJ) out.chapterIds.push_back(jInt(n.find("id")));
		return true;
	}

	// ---- filter input conversion ----

	std::string suwayomiFilterInputsJson(const std::vector<MangaFilter>& defs, const std::vector<MangaFilterValue>& vals)
	{
		// the values list is fit to the defs by mangaFilterFit before this is called; if it isn't, defaults
		// are used, as the same fallback mangafilters.cpp takes
		std::vector<MangaFilterValue> fallback;
		const std::vector<MangaFilterValue>* vp = &vals;
		if (vals.size() != defs.size())
		{
			fallback = mangaFilterDefaults(defs);
			vp = &fallback;
		}
		const std::vector<MangaFilterValue>& values = *vp;
		std::vector<FilterLeaf> leaves;
		std::vector<int> path;
		collectFilterLeaves(defs, values, path, leaves);
		// only what differs from the source's own defaults: the engine starts from those anyway, and a full list
		// trips some sources (Asura Scans: "Expected group change at position 3" once anything else is changed)
		const std::vector<MangaFilterValue> defaults = mangaFilterDefaults(defs);
		std::vector<FilterLeaf> defLeaves;
		path.clear();
		collectFilterLeaves(defs, defaults, path, defLeaves);
		auto same = [](const MangaFilter& d, const MangaFilterValue& a, const MangaFilterValue& b) {
			if (d.type == "select") return a.select == b.select;
			if (d.type == "sort") return a.sortBy == b.sortBy && a.sortAsc == b.sortAsc;
			if (d.type == "check") return a.check == b.check;
			if (d.type == "text") return a.text == b.text;
			if (d.type == "multi") return a.multi == b.multi;
			if (d.type == "tri") return a.tri == b.tri;
			return false;
		};
		JsonWriter w;
		w.arr();
		for (size_t li = 0; li < leaves.size(); li++)
		{
			const FilterLeaf& leaf = leaves[li];
			if (leaf.path.empty() || !leaf.def || !leaf.val) continue;
			if (li < defLeaves.size() && defLeaves[li].val && same(*leaf.def, *leaf.val, *defLeaves[li].val)) continue;
			w.obj();
			w.key("position").num(leaf.path[0]);
			// every group crossed is one nested groupChange; its innermost position names the filter changed
			for (size_t k = 1; k < leaf.path.size(); k++)
			{
				w.key("groupChange").obj();
				w.key("position").num(leaf.path[k]);
			}
			writeFilterState(w, *leaf.def, *leaf.val);
			for (size_t k = 1; k < leaf.path.size(); k++) w.end();
			w.end();
		}
		w.end();
		return w.out();
	}
}
