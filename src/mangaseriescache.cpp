// A source series' cached details and chapters (design 6), pure. src/json.h is a writer only, so the reading lives
// here: small and self-contained, as mangafilters.cpp's, opds.cpp's and suwayomi.cpp's readers are. The file is ours,
// but it sits on a card a person can edit, so anything that isn't this file's shape is no cache rather than an error.
#include "mangaseriescache.h"
#include "json.h"
#include "playlists.h"	// writeFileAtomic: the tmp-then-rename the other stores write with
#include "update.h"		// sha256Hex: the same digest the repositories use
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		constexpr size_t kMaxFile = 8u << 20;	// a source's chapters are capped at 2,000; this is only a guard
		constexpr size_t kMaxChapters = 5000;
		constexpr int kMaxDepth = 64;

		// ---- the JSON reader ----

		struct JVal
		{
			enum Kind { NUL, BOOL, NUM, STR, ARR, OBJ } kind = NUL;
			bool boolean = false;
			double num = 0;
			std::string str;
			std::vector<JVal> items;
			std::vector<std::pair<std::string, JVal>> members;

			const JVal* find(const char* key) const
			{
				for (const auto& kv : members)
					if (kv.first == key) return &kv.second;
				return nullptr;
			}
		};

		void ws(const std::string& s, size_t& p)
		{
			while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) p++;
		}

		void utf8(std::string& out, unsigned cp)
		{
			if (cp < 0x80) out += (char)cp;
			else if (cp < 0x800)
			{
				out += (char)(0xC0 | cp >> 6);
				out += (char)(0x80 | (cp & 0x3F));
			}
			else if (cp < 0x10000)
			{
				out += (char)(0xE0 | cp >> 12);
				out += (char)(0x80 | (cp >> 6 & 0x3F));
				out += (char)(0x80 | (cp & 0x3F));
			}
			else
			{
				out += (char)(0xF0 | cp >> 18);
				out += (char)(0x80 | (cp >> 12 & 0x3F));
				out += (char)(0x80 | (cp >> 6 & 0x3F));
				out += (char)(0x80 | (cp & 0x3F));
			}
		}

		bool hex4(const std::string& s, size_t& p, unsigned& cp)
		{
			if (p + 4 > s.size()) return false;
			cp = 0;
			for (int k = 0; k < 4; k++)
			{
				char c = s[p++];
				if (c >= '0' && c <= '9') cp = cp * 16 + (c - '0');
				else if (c >= 'a' && c <= 'f') cp = cp * 16 + (c - 'a' + 10);
				else if (c >= 'A' && c <= 'F') cp = cp * 16 + (c - 'A' + 10);
				else return false;
			}
			return true;
		}

		bool string(const std::string& s, size_t& p, std::string& out)
		{
			if (p >= s.size() || s[p] != '"') return false;
			p++;
			out.clear();
			while (p < s.size())
			{
				char c = s[p++];
				if (c == '"') return true;
				if ((unsigned char)c < 0x20) return false;	// a raw control character: the escape was meant
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
					unsigned cp = 0;
					if (!hex4(s, p, cp)) return false;
					if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= s.size() && s[p] == '\\' && s[p + 1] == 'u')
					{
						size_t q = p + 2;
						unsigned lo = 0;
						if (hex4(s, q, lo) && lo >= 0xDC00 && lo <= 0xDFFF)
						{
							p = q;
							cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						}
					}
					if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;	// an unpaired half: the writer's replacement
					utf8(out, cp);
					break;
				}
				default: return false;
				}
			}
			return false;
		}

		bool number(const std::string& s, size_t& p, double& out)
		{
			size_t start = p;
			if (p < s.size() && s[p] == '-') p++;
			while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
			if (p < s.size() && s[p] == '.')
			{
				p++;
				while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
			}
			if (p < s.size() && (s[p] == 'e' || s[p] == 'E'))
			{
				p++;
				if (p < s.size() && (s[p] == '+' || s[p] == '-')) p++;
				while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
			}
			if (p == start) return false;
			std::string t = s.substr(start, p - start);
			char* end = nullptr;
			out = strtod(t.c_str(), &end);
			return end && *end == 0;
		}

		bool value(const std::string& s, size_t& p, JVal& v, int depth)
		{
			if (depth > kMaxDepth) return false;
			ws(s, p);
			if (p >= s.size()) return false;
			char c = s[p];
			if (c == '{')
			{
				p++;
				v.kind = JVal::OBJ;
				ws(s, p);
				if (p < s.size() && s[p] == '}') { p++; return true; }
				for (;;)
				{
					std::string k;
					if (!string(s, p, k)) return false;
					ws(s, p);
					if (p >= s.size() || s[p] != ':') return false;
					p++;
					JVal item;
					if (!value(s, p, item, depth + 1)) return false;
					v.members.emplace_back(std::move(k), std::move(item));
					ws(s, p);
					if (p < s.size() && s[p] == ',') { p++; continue; }
					if (p < s.size() && s[p] == '}') { p++; return true; }
					return false;
				}
			}
			if (c == '[')
			{
				p++;
				v.kind = JVal::ARR;
				ws(s, p);
				if (p < s.size() && s[p] == ']') { p++; return true; }
				for (;;)
				{
					JVal item;
					if (!value(s, p, item, depth + 1)) return false;
					v.items.push_back(std::move(item));
					ws(s, p);
					if (p < s.size() && s[p] == ',') { p++; continue; }
					if (p < s.size() && s[p] == ']') { p++; return true; }
					return false;
				}
			}
			if (c == '"')
			{
				v.kind = JVal::STR;
				return string(s, p, v.str);
			}
			if (s.compare(p, 4, "true") == 0) { p += 4; v.kind = JVal::BOOL; v.boolean = true; return true; }
			if (s.compare(p, 5, "false") == 0) { p += 5; v.kind = JVal::BOOL; return true; }
			if (s.compare(p, 4, "null") == 0) { p += 4; return true; }
			v.kind = JVal::NUM;
			return number(s, p, v.num);
		}

		std::string memberStr(const JVal& o, const char* key)
		{
			const JVal* v = o.find(key);
			return v && v->kind == JVal::STR ? v->str : std::string();
		}

		double memberNum(const JVal& o, const char* key)
		{
			const JVal* v = o.find(key);
			return v && v->kind == JVal::NUM ? v->num : 0;
		}

		bool memberBool(const JVal& o, const char* key)
		{
			const JVal* v = o.find(key);
			return v && v->kind == JVal::BOOL && v->boolean;
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

		bool readWhole(const std::string& path, std::string& out)
		{
			FILE* f = fopen(path.c_str(), "rb");
			if (!f) return false;
			out.clear();
			char buf[4096];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, f)) > 0)
			{
				out.append(buf, n);
				if (out.size() > kMaxFile) { fclose(f); return false; }
			}
			fclose(f);
			return true;
		}
	}

	std::string mangaSeriesCachePath(const std::string& stateDir, const std::string& seriesId)
	{
		if (stateDir.empty() || seriesId.empty()) return std::string();
		std::string h = sha256Hex(seriesId);
		if (h.empty()) return std::string();
		return stateDir + "/manga/series/" + h + ".json";
	}

	std::string mangaSeriesCacheJson(const MangaSeriesCache& c)
	{
		JsonWriter w;
		w.obj();
		w.key("format").num(1);
		w.key("fetchedAt").num(c.fetchedAt);
		w.key("detailsOn").boolean(c.detailsOn);
		w.key("chaptersOn").boolean(c.chaptersOn);
		w.key("details").obj();
		w.key("title").str(c.details.title);
		w.key("author").str(c.details.author);
		w.key("artist").str(c.details.artist);
		w.key("description").str(c.details.description);
		w.key("status").str(c.details.status);
		w.key("cover").str(c.details.cover);
		w.key("genres").arr();
		for (const auto& g : c.details.genres) w.str(g);
		w.end();
		w.end();
		w.key("chapters").arr();
		for (const auto& ch : c.chapters)
		{
			w.obj();
			w.key("key").str(ch.key);
			w.key("name").str(ch.name);
			w.key("group").str(ch.group);
			w.key("number").num(ch.number);
			w.key("date").num(ch.date);
			w.key("json").str(ch.json);
			w.end();
		}
		w.end();
		w.end();
		return w.out();
	}

	bool mangaSeriesCacheParse(const std::string& text, MangaSeriesCache& out)
	{
		if (text.empty() || text.size() > kMaxFile) return false;
		size_t p = 0;
		JVal root;
		if (!value(text, p, root, 0) || root.kind != JVal::OBJ) return false;
		ws(text, p);
		if (p != text.size()) return false;	// trailing junk isn't this file
		const JVal* format = root.find("format");
		// a newer format isn't misread as this one: no cache is better than a wrong chapter list
		if (!format || format->kind != JVal::NUM || format->num != 1) return false;

		MangaSeriesCache c;
		c.fetchedAt = (int64_t)memberNum(root, "fetchedAt");
		if (c.fetchedAt < 0) c.fetchedAt = 0;
		c.detailsOn = memberBool(root, "detailsOn");
		c.chaptersOn = memberBool(root, "chaptersOn");
		if (const JVal* d = root.find("details"); d && d->kind == JVal::OBJ)
		{
			c.details.title = memberStr(*d, "title");
			c.details.author = memberStr(*d, "author");
			c.details.artist = memberStr(*d, "artist");
			c.details.description = memberStr(*d, "description");
			c.details.status = memberStr(*d, "status");
			c.details.cover = memberStr(*d, "cover");
			if (const JVal* g = d->find("genres"); g && g->kind == JVal::ARR)
				for (const auto& it : g->items)
					if (it.kind == JVal::STR) c.details.genres.push_back(it.str);
		}
		if (const JVal* a = root.find("chapters"); a && a->kind == JVal::ARR)
		{
			for (const auto& it : a->items)
			{
				if (it.kind != JVal::OBJ || c.chapters.size() >= kMaxChapters) continue;
				MangaChapter ch;
				ch.key = memberStr(it, "key");
				if (ch.key.empty()) continue;	// a chapter the page can't open isn't one to keep
				ch.name = memberStr(it, "name");
				ch.group = memberStr(it, "group");
				ch.number = memberNum(it, "number");
				ch.date = (int64_t)memberNum(it, "date");
				ch.json = memberStr(it, "json");
				c.chapters.push_back(std::move(ch));
			}
		}
		out = std::move(c);
		return true;
	}

	bool mangaSeriesCacheRead(const std::string& stateDir, const std::string& seriesId, MangaSeriesCache& out)
	{
		std::string path = mangaSeriesCachePath(stateDir, seriesId);
		if (path.empty()) return false;
		std::string text;
		if (!readWhole(path, text)) return false;
		return mangaSeriesCacheParse(text, out);
	}

	bool mangaSeriesCacheWrite(const std::string& stateDir, const std::string& seriesId, const MangaSeriesCache& c)
	{
		std::string path = mangaSeriesCachePath(stateDir, seriesId);
		if (path.empty()) return false;
		std::string dir = path.substr(0, path.rfind('/'));
		if (!mkdirP(dir)) return false;
		return writeFileAtomic(path, mangaSeriesCacheJson(c));
	}

	std::string mangaCheckedAgo(int64_t fetchedAt, int64_t now)
	{
		if (fetchedAt <= 0) return std::string();
		int64_t age = now - fetchedAt;
		if (age < 86400) return "today";	// also a clock behind the file's stamp
		int64_t days = age / 86400;
		if (days == 1) return "1 day";
		return std::to_string(days) + " days";
	}

	bool mangaCheckedStale(int64_t fetchedAt, int64_t now)
	{
		return fetchedAt > 0 && now - fetchedAt >= 86400;
	}
}
