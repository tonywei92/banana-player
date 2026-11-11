#include "mangafilters.h"
#include "json.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace bp
{
	namespace
	{
		// bpcore's json.h only writes, and the sheet must read the host's normalized filter list. This is the small
		// reader that list needs: objects, arrays, strings, numbers, booleans and null, nothing else.
		struct JVal
		{
			enum Type { Nil, Bool, Num, Str, Arr, Obj } type = Nil;
			bool b = false;
			double n = 0;
			std::string s;
			std::vector<JVal> a;
			std::vector<std::pair<std::string, JVal>> o;

			const JVal* find(const std::string& key) const
			{
				for (const auto& kv : o)
					if (kv.first == key) return &kv.second;
				return nullptr;
			}
		};

		void appendUtf8(std::string& out, uint32_t cp)
		{
			if (cp <= 0x7F) out += (char)cp;
			else if (cp <= 0x7FF)
			{
				out += (char)(0xC0 | (cp >> 6));
				out += (char)(0x80 | (cp & 0x3F));
			}
			else if (cp <= 0xFFFF)
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

		struct JParser
		{
			explicit JParser(const std::string& text) : src(text) {}

			const std::string& src;
			size_t at = 0;
			std::string err;

			bool fail(const char* what)
			{
				if (err.empty()) err = std::string(what) + " at byte " + std::to_string(at);
				return false;
			}

			void space()
			{
				while (at < src.size() && (src[at] == ' ' || src[at] == '\t' || src[at] == '\n' || src[at] == '\r')) at++;
			}

			bool hex4(uint32_t& out)
			{
				if (at + 4 > src.size()) return fail("a short \\u escape");
				out = 0;
				for (int i = 0; i < 4; i++)
				{
					char c = src[at++];
					out <<= 4;
					if (c >= '0' && c <= '9') out += (uint32_t)(c - '0');
					else if (c >= 'a' && c <= 'f') out += (uint32_t)(c - 'a' + 10);
					else if (c >= 'A' && c <= 'F') out += (uint32_t)(c - 'A' + 10);
					else return fail("a bad \\u escape");
				}
				return true;
			}

			bool string(std::string& out)
			{
				at++;	// the opening quote
				while (at < src.size())
				{
					unsigned char c = (unsigned char)src[at++];
					if (c == '"') return true;
					if (c != '\\') { out += (char)c; continue; }
					if (at >= src.size()) return fail("an unterminated escape");
					char e = src[at++];
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
						uint32_t cp = 0;
						if (!hex4(cp)) return false;
						// a pair of surrogates is one code point; a lone one becomes U+FFFD (never valid text)
						if (cp >= 0xD800 && cp <= 0xDBFF && at + 6 <= src.size() && src[at] == '\\' && src[at + 1] == 'u')
						{
							size_t keep = at;
							at += 2;
							uint32_t lo = 0;
							if (!hex4(lo)) return false;
							if (lo >= 0xDC00 && lo <= 0xDFFF)
								cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
							else
							{
								at = keep;
								cp = 0xFFFD;
							}
						}
						else if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
						appendUtf8(out, cp);
						break;
					}
					default: return fail("a bad escape");
					}
				}
				return fail("an unterminated string");
			}

			bool number(JVal& out)
			{
				size_t start = at;
				if (src[at] == '-') at++;
				while (at < src.size() && std::isdigit((unsigned char)src[at])) at++;
				if (at < src.size() && src[at] == '.')
				{
					at++;
					while (at < src.size() && std::isdigit((unsigned char)src[at])) at++;
				}
				if (at < src.size() && (src[at] == 'e' || src[at] == 'E'))
				{
					at++;
					if (at < src.size() && (src[at] == '+' || src[at] == '-')) at++;
					while (at < src.size() && std::isdigit((unsigned char)src[at])) at++;
				}
				if (at == start) return fail("a number expected");
				out.type = JVal::Num;
				out.n = std::strtod(src.substr(start, at - start).c_str(), nullptr);
				return true;
			}

			bool value(JVal& out, int depth)
			{
				if (depth > 32) return fail("nested too deeply");
				space();
				if (at >= src.size()) return fail("a value expected");
				char c = src[at];
				if (c == '{') return object(out, depth);
				if (c == '[') return array(out, depth);
				if (c == '"')
				{
					out.type = JVal::Str;
					return string(out.s);
				}
				if (src.compare(at, 4, "true") == 0) { at += 4; out.type = JVal::Bool; out.b = true; return true; }
				if (src.compare(at, 5, "false") == 0) { at += 5; out.type = JVal::Bool; out.b = false; return true; }
				if (src.compare(at, 4, "null") == 0) { at += 4; out.type = JVal::Nil; return true; }
				if (c == '-' || std::isdigit((unsigned char)c)) return number(out);
				return fail("a value expected");
			}

			bool object(JVal& out, int depth)
			{
				out.type = JVal::Obj;
				at++;
				space();
				if (at < src.size() && src[at] == '}') { at++; return true; }
				while (true)
				{
					space();
					if (at >= src.size() || src[at] != '"') return fail("a key expected");
					std::string key;
					if (!string(key)) return false;
					space();
					if (at >= src.size() || src[at] != ':') return fail("':' expected");
					at++;
					JVal v;
					if (!value(v, depth + 1)) return false;
					out.o.emplace_back(std::move(key), std::move(v));
					space();
					if (at >= src.size()) return fail("an unterminated object");
					if (src[at] == '}') { at++; return true; }
					if (src[at] != ',') return fail("',' expected");
					at++;
				}
			}

			bool array(JVal& out, int depth)
			{
				out.type = JVal::Arr;
				at++;
				space();
				if (at < src.size() && src[at] == ']') { at++; return true; }
				while (true)
				{
					JVal v;
					if (!value(v, depth + 1)) return false;
					out.a.push_back(std::move(v));
					space();
					if (at >= src.size()) return fail("an unterminated array");
					if (src[at] == ']') { at++; return true; }
					if (src[at] != ',') return fail("',' expected");
					at++;
				}
			}

			bool parse(JVal& out)
			{
				if (!value(out, 0)) return false;
				space();
				if (at != src.size()) return fail("text after the value");
				return true;
			}
		};

		bool knownType(const std::string& type)
		{
			return type == "select" || type == "sort" || type == "check" || type == "multi" || type == "tri" ||
				type == "text" || type == "group";
		}

		bool optionId(const MangaFilter& f, const std::string& id)
		{
			for (const auto& o : f.options)
				if (o.id == id) return true;
			return false;
		}

		std::string defaultOptionId(const MangaFilter& f)
		{
			const std::string& want = f.type == "sort" ? f.sortBy : f.textDefault;
			if (!want.empty() && optionId(f, want)) return want;
			return f.options.empty() ? "" : f.options.front().id;
		}

		void readStrings(const JVal* v, std::vector<std::string>& out)
		{
			out.clear();
			if (!v || v->type != JVal::Arr) return;
			for (const auto& e : v->a)
				if (e.type == JVal::Str && !e.s.empty()) out.push_back(e.s);
		}

		bool parseList(const JVal& list, std::vector<MangaFilter>& out, std::string& error, int depth);

		bool parseOne(const JVal& item, MangaFilter& f, std::string& error, int depth)
		{
			const JVal* id = item.find("id");
			const JVal* type = item.find("type");
			if (!id || id->type != JVal::Str || id->s.empty()) return false;
			if (!type || type->type != JVal::Str || !knownType(type->s)) return false;
			f.id = id->s;
			f.type = type->s;
			const JVal* name = item.find("name");
			f.name = name && name->type == JVal::Str && !name->s.empty() ? name->s : f.id;
			const JVal* def = item.find("default");
			if (f.type == "group")
			{
				const JVal* kids = item.find("filters");
				if (kids && kids->type == JVal::Arr)
				{
					if (!parseList(*kids, f.children, error, depth + 1)) return false;
				}
				return true;
			}
			const JVal* opts = item.find("options");
			if (opts && opts->type == JVal::Arr)
			{
				for (const auto& o : opts->a)
				{
					MangaFilterOption opt;
					if (o.type == JVal::Str)
					{
						if (o.s.empty()) continue;
						opt.id = opt.name = o.s;
					}
					else if (o.type == JVal::Obj)
					{
						const JVal* oi = o.find("id");
						if (!oi || oi->type != JVal::Str || oi->s.empty()) continue;
						opt.id = oi->s;
						const JVal* on = o.find("name");
						opt.name = on && on->type == JVal::Str && !on->s.empty() ? on->s : opt.id;
					}
					else continue;
					f.options.push_back(std::move(opt));
				}
			}
			if (f.type == "select")
			{
				if (def && def->type == JVal::Str) f.textDefault = def->s;
				else if (def && def->type == JVal::Num && def->n >= 0 && def->n < (double)f.options.size())
					f.textDefault = f.options[(size_t)def->n].id;
			}
			else if (f.type == "sort")
			{
				if (def && def->type == JVal::Obj)
				{
					const JVal* by = def->find("by");
					const JVal* asc = def->find("asc");
					if (by && by->type == JVal::Str && optionId(f, by->s))
					{
						f.sortHasDefault = true;
						f.sortBy = by->s;
						f.sortAsc = asc && asc->type == JVal::Bool ? asc->b : false;
					}
				}
			}
			else if (f.type == "check")
			{
				if (def && def->type == JVal::Bool) f.checkDefault = def->b;
			}
			else if (f.type == "text")
			{
				if (def && def->type == JVal::Str) f.textDefault = def->s;
			}
			else if (f.type == "multi")
			{
				readStrings(def, f.multiDefault);
			}
			else if (f.type == "tri" && def && def->type == JVal::Obj)
			{
				readStrings(def->find("include"), f.includeDefault);
				readStrings(def->find("exclude"), f.excludeDefault);
			}
			return true;
		}

		bool parseList(const JVal& list, std::vector<MangaFilter>& out, std::string& error, int depth)
		{
			if (list.type != JVal::Arr)
			{
				error = "the filters aren't a list";
				return false;
			}
			for (const auto& item : list.a)
			{
				if (item.type != JVal::Obj) continue;
				MangaFilter f;
				if (!parseOne(item, f, error, depth)) continue;
				out.push_back(std::move(f));
			}
			return true;
		}

		void writeValues(JsonWriter& w, const std::vector<MangaFilter>& defs,
			const std::vector<MangaFilterValue>& values)
		{
			// a caller that didn't fit them still gets a usable object: the definitions' defaults
			std::vector<MangaFilterValue> fallback;
			const std::vector<MangaFilterValue>* vp = &values;
			if (values.size() != defs.size())
			{
				fallback = mangaFilterDefaults(defs);
				vp = &fallback;
			}
			const std::vector<MangaFilterValue>& vals = *vp;
			w.obj();
			for (size_t i = 0; i < defs.size(); i++)
			{
				const MangaFilter& d = defs[i];
				const MangaFilterValue& v = vals[i];
				if (d.type == "group")
				{
					w.key(d.id);
					writeValues(w, d.children, v.children);
					continue;
				}
				if (d.type == "select")
				{
					std::string id = optionId(d, v.select) ? v.select : defaultOptionId(d);
					w.key(d.id).str(id);
				}
				else if (d.type == "sort")
				{
					std::string by = optionId(d, v.sortBy) ? v.sortBy : defaultOptionId(d);
					w.key(d.id).obj().key("by").str(by).key("asc").boolean(v.sortAsc).end();
				}
				else if (d.type == "check") w.key(d.id).boolean(v.check);
				else if (d.type == "multi")
				{
					w.key(d.id).arr();
					for (size_t k = 0; k < d.options.size() && k < v.multi.size(); k++)
						if (v.multi[k]) w.str(d.options[k].id);
					w.end();
				}
				else if (d.type == "tri")
				{
					w.key(d.id).obj().key("include").arr();
					for (size_t k = 0; k < d.options.size() && k < v.tri.size(); k++)
						if (v.tri[k] == 1) w.str(d.options[k].id);
					w.end().key("exclude").arr();
					for (size_t k = 0; k < d.options.size() && k < v.tri.size(); k++)
						if (v.tri[k] == 2) w.str(d.options[k].id);
					w.end().end();
				}
				else if (d.type == "text")
				{
					std::string t = v.text;
					size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
					t = a == std::string::npos ? "" : t.substr(a, b - a + 1);
					w.key(d.id).str(t);
				}
			}
			w.end();
		}

		void countTicks(const std::vector<MangaFilter>& defs, const std::vector<MangaFilterValue>& values, int& yes, int& no)
		{
			for (size_t i = 0; i < defs.size() && i < values.size(); i++)
			{
				const MangaFilter& d = defs[i];
				const MangaFilterValue& v = values[i];
				if (d.type == "group") countTicks(d.children, v.children, yes, no);
				else if (d.type == "multi")
				{
					for (int m : v.multi) if (m) yes++;
				}
				else if (d.type == "tri")
				{
					for (int t : v.tri)
					{
						if (t == 1) yes++;
						else if (t == 2) no++;
					}
				}
			}
		}
	}

	bool mangaFiltersParse(const std::string& json, std::vector<MangaFilter>& out, std::string& error)
	{
		out.clear();
		error.clear();
		JVal root;
		JParser p(json);
		if (!p.parse(root))
		{
			error = p.err;
			return false;
		}
		return parseList(root, out, error, 0);
	}

	std::vector<MangaFilterValue> mangaFilterDefaults(const std::vector<MangaFilter>& defs)
	{
		std::vector<MangaFilterValue> out(defs.size());
		for (size_t i = 0; i < defs.size(); i++)
		{
			const MangaFilter& d = defs[i];
			MangaFilterValue& v = out[i];
			if (d.type == "select") v.select = defaultOptionId(d);
			else if (d.type == "sort")
			{
				v.sortBy = defaultOptionId(d);
				// a sort that names no direction is taken as descending: sites put newest or most popular first
				v.sortAsc = d.sortHasDefault && d.sortAsc;
			}
			else if (d.type == "check") v.check = d.checkDefault;
			else if (d.type == "text") v.text = d.textDefault;
			else if (d.type == "multi")
			{
				v.multi.assign(d.options.size(), 0);
				for (size_t k = 0; k < d.options.size(); k++)
					for (const auto& id : d.multiDefault)
						if (d.options[k].id == id) v.multi[k] = 1;
			}
			else if (d.type == "tri")
			{
				v.tri.assign(d.options.size(), 0);
				for (size_t k = 0; k < d.options.size(); k++)
				{
					for (const auto& id : d.includeDefault)
						if (d.options[k].id == id) v.tri[k] = 1;
					for (const auto& id : d.excludeDefault)
						if (d.options[k].id == id) v.tri[k] = 2;
				}
			}
			else if (d.type == "group") v.children = mangaFilterDefaults(d.children);
		}
		return out;
	}

	void mangaFilterFit(const std::vector<MangaFilter>& defs, std::vector<MangaFilterValue>& values)
	{
		if (values.size() != defs.size()) values = mangaFilterDefaults(defs);
		for (size_t i = 0; i < defs.size(); i++)
		{
			const MangaFilter& d = defs[i];
			MangaFilterValue& v = values[i];
			if (d.type == "select")
			{
				if (!optionId(d, v.select)) v.select = defaultOptionId(d);
			}
			else if (d.type == "sort")
			{
				if (!optionId(d, v.sortBy)) v.sortBy = defaultOptionId(d);
			}
			else if (d.type == "group")
			{
				if (v.children.size() != d.children.size()) v.children = mangaFilterDefaults(d.children);
				else mangaFilterFit(d.children, v.children);
			}
			else if (d.type == "multi") v.multi.resize(d.options.size(), 0);
			else if (d.type == "tri") v.tri.resize(d.options.size(), 0);
		}
	}

	std::string mangaFilterValuesJson(const std::vector<MangaFilter>& defs, const std::vector<MangaFilterValue>& values)
	{
		JsonWriter w;
		writeValues(w, defs, values);
		return w.out();
	}

	std::string mangaFilterSummary(const MangaFilter& def, const MangaFilterValue& value)
	{
		if (def.type == "group")
		{
			int yes = 0, no = 0;
			countTicks(def.children, value.children, yes, no);
			if (!yes && !no) return "None";
			std::string s;
			if (yes) s = "\xE2\x9C\x93" + std::to_string(yes);
			if (no) s += (s.empty() ? "" : " ") + std::string("\xE2\x9C\x97") + std::to_string(no);
			return s;
		}
		if (def.type == "select" || def.type == "sort")
		{
			std::string id = def.type == "select" ? value.select : value.sortBy;
			for (const auto& o : def.options)
				if (o.id == id) return o.name;
			return "\xE2\x80\x94";	// "—": no option
		}
		if (def.type == "check") return value.check ? "On" : "Off";
		if (def.type == "text")
		{
			std::string t = value.text;
			size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
			t = a == std::string::npos ? "" : t.substr(a, b - a + 1);
			// the row has one line: 60 characters is the same ceiling the source's labels get (5.2.1)
			if (t.size() > 60)
			{
				size_t n = 60;
				while (n > 0 && ((unsigned char)t[n] & 0xC0) == 0x80) n--;
				t = t.substr(0, n) + "\xE2\x80\xA6";
			}
			return t.empty() ? "\xE2\x80\x94" : t;
		}
		if (def.type == "multi")
		{
			int n = 0;
			for (int m : value.multi) if (m) n++;
			return n ? "\xE2\x9C\x93" + std::to_string(n) : "None";
		}
		if (def.type == "tri")
		{
			int yes = 0, no = 0;
			for (int t : value.tri)
			{
				if (t == 1) yes++;
				else if (t == 2) no++;
			}
			if (!yes && !no) return "None";
			std::string s;
			if (yes) s = "\xE2\x9C\x93" + std::to_string(yes);
			if (no) s += (s.empty() ? "" : " ") + std::string("\xE2\x9C\x97") + std::to_string(no);
			return s;
		}
		return "";
	}
}
