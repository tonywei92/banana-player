#include "mangasource.h"
#include "htmldoc.h"	// bp.html's document; lexbor stays behind it (src/htmldoc.h), so this host needs nothing else
extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <set>
#include <sstream>
#include <utility>

namespace
{
	constexpr size_t kMaxList = 2000;
	constexpr size_t kMaxString = 64 * 1024;
	constexpr size_t kMaxPrefs = 64 * 1024;
	constexpr size_t kMaxResponse = 8 * 1024 * 1024;
	constexpr int kMaxRequestsPerCall = 40;
	constexpr int kMaxJsonDepth = 100;
	constexpr int kHookInstructions = 10000;

	// The allocator's own ledger. Lua's osize is real for realloc/free; for a fresh block ptr is null and osize is a
	// Lua type tag, so that case must not use it.
	struct MemCtx
	{
		size_t used = 0, cap = 0;
	};

	void* cappedAlloc(void* ud, void* ptr, size_t osize, size_t nsize)
	{
		MemCtx* m = static_cast<MemCtx*>(ud);
		if (!ptr)
		{
			if (m->used > m->cap || nsize > m->cap - m->used) return nullptr;
			void* p = std::malloc(nsize);
			if (!p) return nullptr;
			m->used += nsize;
			return p;
		}
		if (nsize == 0)
		{
			m->used -= osize;
			std::free(ptr);
			return nullptr;
		}
		if (nsize > osize && (m->used > m->cap || nsize - osize > m->cap - m->used)) return nullptr;
		void* p = std::realloc(ptr, nsize);
		if (!p) return nullptr;
		if (nsize > osize) m->used += nsize - osize;
		else m->used -= osize - nsize;
		return p;
	}

	// A JSON value of our own: bpcore's json.h writes only, and the host must read filters, series and prefs files.
	struct JVal
	{
		enum Type { Nil, Bool, Num, Str, Arr, Obj };
		Type type = Nil;
		bool b = false;
		double n = 0;
		bool isInt = false;
		int64_t i = 0;
		std::string s;
		std::vector<JVal> a;
		std::vector<std::pair<std::string, JVal>> o;

		const JVal* find(const std::string& k) const
		{
			for (const auto& kv : o)
				if (kv.first == k) return &kv.second;
			return nullptr;
		}
	};

	void appendUtf8(std::string& out, uint32_t cp)
	{
		if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
		if (cp <= 0x7F) out += char(cp);
		else if (cp <= 0x7FF)
		{
			out += char(0xC0 | (cp >> 6));
			out += char(0x80 | (cp & 0x3F));
		}
		else if (cp <= 0xFFFF)
		{
			out += char(0xE0 | (cp >> 12));
			out += char(0x80 | ((cp >> 6) & 0x3F));
			out += char(0x80 | (cp & 0x3F));
		}
		else
		{
			out += char(0xF0 | (cp >> 18));
			out += char(0x80 | ((cp >> 12) & 0x3F));
			out += char(0x80 | ((cp >> 6) & 0x3F));
			out += char(0x80 | (cp & 0x3F));
		}
	}

	class JsonParser
	{
	public:
		explicit JsonParser(const std::string& text) : s_(text) {}
		bool parse(JVal& out)
		{
			skip();
			if (!value(out, 0)) return false;
			skip();
			if (p_ != s_.size()) return fail("trailing text");
			return true;
		}
		const std::string& error() const { return err_; }

	private:
		bool fail(const char* what)
		{
			if (err_.empty()) err_ = std::string(what) + " at byte " + std::to_string(p_);
			return false;
		}
		void skip()
		{
			while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t' || s_[p_] == '\n' || s_[p_] == '\r')) p_++;
		}
		static int hex(char c)
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		}
		bool hex4(uint32_t& out)
		{
			if (p_ + 4 > s_.size()) return fail("short \\u escape");
			uint32_t v = 0;
			for (int k = 0; k < 4; k++)
			{
				int h = hex(s_[p_++]);
				if (h < 0) return fail("bad \\u escape");
				v = (v << 4) | uint32_t(h);
			}
			out = v;
			return true;
		}
		bool string(std::string& out)
		{
			if (p_ >= s_.size() || s_[p_] != '"') return fail("expected a string");
			p_++;
			while (p_ < s_.size())
			{
				unsigned char c = (unsigned char)s_[p_++];
				if (c == '"') return true;
				if (c < 0x20) return fail("control character in a string");
				if (c != '\\')
				{
					out += char(c);
					continue;
				}
				if (p_ >= s_.size()) return fail("unterminated escape");
				char e = s_[p_++];
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
					if (cp >= 0xD800 && cp <= 0xDBFF)
					{
						// A surrogate pair; a lone half becomes U+FFFD as the writer does.
						if (p_ + 6 <= s_.size() && s_[p_] == '\\' && s_[p_ + 1] == 'u')
						{
							size_t save = p_;
							p_ += 2;
							uint32_t lo = 0;
							if (!hex4(lo)) return false;
							if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
							else p_ = save;
						}
					}
					appendUtf8(out, cp);
					break;
				}
				default: return fail("unknown escape");
				}
			}
			return fail("unterminated string");
		}
		bool number(JVal& out)
		{
			size_t start = p_;
			if (p_ < s_.size() && s_[p_] == '-') p_++;
			if (p_ >= s_.size()) return fail("empty number");
			if (s_[p_] == '0') p_++;
			else if (s_[p_] >= '1' && s_[p_] <= '9')
			{
				while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') p_++;
			}
			else return fail("bad number");
			bool integral = true;
			if (p_ < s_.size() && s_[p_] == '.')
			{
				integral = false;
				p_++;
				if (p_ >= s_.size() || s_[p_] < '0' || s_[p_] > '9') return fail("bad fraction");
				while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') p_++;
			}
			if (p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E'))
			{
				integral = false;
				p_++;
				if (p_ < s_.size() && (s_[p_] == '+' || s_[p_] == '-')) p_++;
				if (p_ >= s_.size() || s_[p_] < '0' || s_[p_] > '9') return fail("bad exponent");
				while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') p_++;
			}
			std::string token = s_.substr(start, p_ - start);
			out.type = JVal::Num;
			if (integral)
			{
				errno = 0;
				char* end = nullptr;
				long long v = std::strtoll(token.c_str(), &end, 10);
				if (errno == 0 && end && *end == '\0')
				{
					out.isInt = true;
					out.i = v;
					out.n = double(v);
					return true;
				}
			}
			out.isInt = false;
			out.n = std::strtod(token.c_str(), nullptr);
			return true;
		}
		bool value(JVal& out, int depth)
		{
			if (depth > kMaxJsonDepth) return fail("nested too deeply");
			skip();
			if (p_ >= s_.size()) return fail("unexpected end");
			char c = s_[p_];
			if (c == '{')
			{
				p_++;
				out.type = JVal::Obj;
				skip();
				if (p_ < s_.size() && s_[p_] == '}') { p_++; return true; }
				while (true)
				{
					skip();
					std::string key;
					if (!string(key)) return false;
					skip();
					if (p_ >= s_.size() || s_[p_] != ':') return fail("expected ':'");
					p_++;
					JVal v;
					if (!value(v, depth + 1)) return false;
					out.o.emplace_back(std::move(key), std::move(v));
					skip();
					if (p_ < s_.size() && s_[p_] == ',') { p_++; continue; }
					if (p_ < s_.size() && s_[p_] == '}') { p_++; return true; }
					return fail("expected ',' or '}'");
				}
			}
			if (c == '[')
			{
				p_++;
				out.type = JVal::Arr;
				skip();
				if (p_ < s_.size() && s_[p_] == ']') { p_++; return true; }
				while (true)
				{
					JVal v;
					if (!value(v, depth + 1)) return false;
					out.a.push_back(std::move(v));
					skip();
					if (p_ < s_.size() && s_[p_] == ',') { p_++; continue; }
					if (p_ < s_.size() && s_[p_] == ']') { p_++; return true; }
					return fail("expected ',' or ']'");
				}
			}
			if (c == '"')
			{
				out.type = JVal::Str;
				return string(out.s);
			}
			if (s_.compare(p_, 4, "null") == 0) { p_ += 4; out.type = JVal::Nil; return true; }
			if (s_.compare(p_, 4, "true") == 0) { p_ += 4; out.type = JVal::Bool; out.b = true; return true; }
			if (s_.compare(p_, 5, "false") == 0) { p_ += 5; out.type = JVal::Bool; out.b = false; return true; }
			if (c == '-' || (c >= '0' && c <= '9')) return number(out);
			return fail("unexpected character");
		}

		const std::string& s_;
		size_t p_ = 0;
		std::string err_;
	};

	void encodeJson(const JVal& v, std::string& out)
	{
		switch (v.type)
		{
		case JVal::Nil: out += "null"; break;
		case JVal::Bool: out += v.b ? "true" : "false"; break;
		case JVal::Num:
			if (v.isInt) out += std::to_string(v.i);
			else if (!std::isfinite(v.n)) out += "null";
			else
			{
				char buf[40];
				std::snprintf(buf, sizeof buf, "%.17g", v.n);
				out += buf;
			}
			break;
		case JVal::Str:
			out += '"';
			for (unsigned char c : v.s)
			{
				switch (c)
				{
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\b': out += "\\b"; break;
				case '\f': out += "\\f"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (c < 0x20)
					{
						char buf[8];
						std::snprintf(buf, sizeof buf, "\\u%04x", c);
						out += buf;
					}
					else out += char(c);
				}
			}
			out += '"';
			break;
		case JVal::Arr:
			out += '[';
			for (size_t k = 0; k < v.a.size(); k++)
			{
				if (k) out += ',';
				encodeJson(v.a[k], out);
			}
			out += ']';
			break;
		case JVal::Obj:
			out += '{';
			for (size_t k = 0; k < v.o.size(); k++)
			{
				if (k) out += ',';
				JVal key;
				key.type = JVal::Str;
				key.s = v.o[k].first;
				encodeJson(key, out);
				out += ':';
				encodeJson(v.o[k].second, out);
			}
			out += '}';
			break;
		}
	}

	bool parseJson(const std::string& text, JVal& out, std::string& error)
	{
		JsonParser p(text);
		if (!p.parse(out))
		{
			error = p.error();
			return false;
		}
		return true;
	}

	const char* luaTypeName(int t)
	{
		switch (t)
		{
		case LUA_TNIL: return "nil";
		case LUA_TBOOLEAN: return "boolean";
		case LUA_TNUMBER: return "number";
		case LUA_TSTRING: return "string";
		case LUA_TTABLE: return "table";
		case LUA_TFUNCTION: return "function";
		case LUA_TTHREAD: return "thread";
		case LUA_TUSERDATA: return "userdata";
		default: return "value";
		}
	}

	bool luaToJson(lua_State* L, int idx, JVal& out, std::string& error, int depth,
		std::set<const void*>& seen)
	{
		idx = lua_absindex(L, idx);
		if (depth > kMaxJsonDepth)
		{
			error = "table nested too deeply";
			return false;
		}
		switch (lua_type(L, idx))
		{
		case LUA_TNIL: out.type = JVal::Nil; return true;
		case LUA_TBOOLEAN: out.type = JVal::Bool; out.b = lua_toboolean(L, idx) != 0; return true;
		case LUA_TNUMBER:
			out.type = JVal::Num;
			if (lua_isinteger(L, idx))
			{
				out.isInt = true;
				out.i = (int64_t)lua_tointeger(L, idx);
				out.n = double(out.i);
			}
			else out.n = lua_tonumber(L, idx);
			return true;
		case LUA_TSTRING:
		{
			size_t n = 0;
			const char* s = lua_tolstring(L, idx, &n);
			out.type = JVal::Str;
			out.s.assign(s, n);
			return true;
		}
		case LUA_TTABLE:
		{
			const void* ptr = lua_topointer(L, idx);
			if (seen.count(ptr))
			{
				error = "table refers to itself";
				return false;
			}
			seen.insert(ptr);
			bool any = false, seq = true;
			size_t count = 0;
			lua_Integer max = 0;
			lua_pushnil(L);
			while (lua_next(L, idx))
			{
				any = true;
				count++;
				if (!lua_isinteger(L, -2)) seq = false;
				else
				{
					lua_Integer k = lua_tointeger(L, -2);
					if (k < 1) seq = false;
					else if (k > max) max = k;
				}
				lua_pop(L, 1);
			}
			bool ok = true;
			if (seq && any && max == (lua_Integer)count)
			{
				out.type = JVal::Arr;
				out.a.reserve(count);
				for (lua_Integer k = 1; k <= max && ok; k++)
				{
					lua_rawgeti(L, idx, k);
					JVal v;
					ok = luaToJson(L, -1, v, error, depth + 1, seen);
					lua_pop(L, 1);
					if (ok) out.a.push_back(std::move(v));
				}
			}
			else
			{
				out.type = JVal::Obj;
				lua_pushnil(L);
				while (lua_next(L, idx))
				{
					std::string key;
					int kt = lua_type(L, -2);
					if (kt == LUA_TSTRING || lua_isnumber(L, -2))
					{
						size_t n = 0;
						lua_pushvalue(L, -2);	// tostring must not change the key lua_next is holding
						const char* s = lua_tolstring(L, -1, &n);
						key.assign(s, n);
						lua_pop(L, 1);
					}
					else
					{
						error = std::string("a table key is a ") + luaTypeName(kt);
						ok = false;
						lua_pop(L, 1);
						break;
					}
					JVal v;
					if (!luaToJson(L, -1, v, error, depth + 1, seen)) ok = false;
					lua_pop(L, 1);
					if (!ok) break;
					out.o.emplace_back(std::move(key), std::move(v));
				}
			}
			seen.erase(ptr);
			return ok;
		}
		default:
			error = std::string("can't encode a ") + luaTypeName(lua_type(L, idx));
			return false;
		}
	}

	bool pushJson(lua_State* L, const JVal& v, int depth)
	{
		if (depth > kMaxJsonDepth) return false;
		if (!lua_checkstack(L, 4)) return false;
		switch (v.type)
		{
		case JVal::Nil: lua_pushnil(L); return true;
		case JVal::Bool: lua_pushboolean(L, v.b ? 1 : 0); return true;
		case JVal::Num:
			if (v.isInt) lua_pushinteger(L, (lua_Integer)v.i);
			else lua_pushnumber(L, v.n);
			return true;
		case JVal::Str: lua_pushlstring(L, v.s.data(), v.s.size()); return true;
		case JVal::Arr:
			lua_createtable(L, (int)v.a.size(), 0);
			for (size_t k = 0; k < v.a.size(); k++)
			{
				if (!pushJson(L, v.a[k], depth + 1)) return false;
				lua_rawseti(L, -2, (lua_Integer)k + 1);
			}
			return true;
		case JVal::Obj:
			lua_createtable(L, 0, (int)v.o.size());
			for (const auto& kv : v.o)
			{
				lua_pushlstring(L, kv.first.data(), kv.first.size());
				if (!pushJson(L, kv.second, depth + 1)) return false;
				lua_settable(L, -3);
			}
			return true;
		}
		return false;
	}

	std::string clipBytes(std::string s, size_t max)
	{
		if (s.size() > max) s.resize(max);
		return s;
	}

	// Labels are limited by characters, not bytes, so a clipped UTF-8 name stays valid.
	std::string clipUtf8(const std::string& s, size_t maxChars)
	{
		size_t i = 0, chars = 0;
		while (i < s.size() && chars < maxChars)
		{
			unsigned char c = (unsigned char)s[i];
			size_t len = 1;
			if ((c & 0x80) == 0) len = 1;
			else if ((c & 0xE0) == 0xC0) len = 2;
			else if ((c & 0xF0) == 0xE0) len = 3;
			else if ((c & 0xF8) == 0xF0) len = 4;
			if (i + len > s.size()) len = 1;
			i += len;
			chars++;
		}
		return s.substr(0, i);
	}

	std::string lower(std::string s)
	{
		for (char& c : s)
			if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
		return s;
	}

	// Lua field readers. A missing optional field leaves `out` alone.
	bool getStringField(lua_State* L, int tbl, const char* key, std::string& out, bool required,
		std::string& error)
	{
		tbl = lua_absindex(L, tbl);
		lua_getfield(L, tbl, key);
		if (lua_isnil(L, -1))
		{
			lua_pop(L, 1);
			if (required) error = std::string("the source has no ") + key;
			return !required;
		}
		if (lua_type(L, -1) != LUA_TSTRING && !lua_isnumber(L, -1))
		{
			lua_pop(L, 1);
			error = std::string("the source's ") + key + " isn't a string";
			return false;
		}
		size_t n = 0;
		const char* s = lua_tolstring(L, -1, &n);
		out.assign(s, n);
		lua_pop(L, 1);
		return true;
	}

	bool getBoolField(lua_State* L, int tbl, const char* key, bool& out)
	{
		tbl = lua_absindex(L, tbl);
		lua_getfield(L, tbl, key);
		bool ok = !lua_isnil(L, -1);
		if (ok) out = lua_toboolean(L, -1) != 0;
		lua_pop(L, 1);
		return ok;
	}

	void readHeaders(lua_State* L, int idx, std::map<std::string, std::string>& out)
	{
		if (!lua_istable(L, idx)) return;
		idx = lua_absindex(L, idx);
		lua_pushnil(L);
		while (lua_next(L, idx))
		{
			if (lua_type(L, -2) == LUA_TSTRING &&
				(lua_type(L, -1) == LUA_TSTRING || lua_isnumber(L, -1)))
			{
				size_t kn = 0, vn = 0;
				const char* k = lua_tolstring(L, -2, &kn);
				const char* v = lua_tolstring(L, -1, &vn);
				out[std::string(k, kn)] = std::string(v, vn);
			}
			lua_pop(L, 1);
		}
	}

	std::string urlEncode(const std::string& s)
	{
		static const char* hex = "0123456789ABCDEF";
		std::string out;
		for (unsigned char c : s)
		{
			bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
				c == '-' || c == '_' || c == '.' || c == '~';
			if (unreserved) out += char(c);
			else
			{
				out += '%';
				out += hex[c >> 4];
				out += hex[c & 0xF];
			}
		}
		return out;
	}

	int hexDigit(char c)
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	}

	std::string urlDecode(const std::string& s)
	{
		std::string out;
		for (size_t i = 0; i < s.size(); i++)
		{
			if (s[i] == '%' && i + 2 < s.size())
			{
				int a = hexDigit(s[i + 1]), b = hexDigit(s[i + 2]);
				if (a >= 0 && b >= 0)
				{
					out += char((a << 4) | b);
					i += 2;
					continue;
				}
			}
			out += s[i];
		}
		return out;
	}

	struct UrlParts
	{
		std::string scheme, authority, path, query;
		bool hasAuthority = false, hasQuery = false;
	};

	bool hasScheme(const std::string& s)
	{
		if (s.empty() || !std::isalpha((unsigned char)s[0])) return false;
		for (size_t i = 1; i < s.size(); i++)
		{
			char c = s[i];
			if (c == ':') return true;
			if (!(std::isalnum((unsigned char)c) || c == '+' || c == '-' || c == '.')) return false;
		}
		return false;
	}

	UrlParts splitUrl(const std::string& s)
	{
		UrlParts u;
		size_t p = 0;
		if (hasScheme(s))
		{
			size_t c = s.find(':');
			u.scheme = s.substr(0, c);
			p = c + 1;
		}
		if (p + 1 < s.size() && s[p] == '/' && s[p + 1] == '/')
		{
			u.hasAuthority = true;
			p += 2;
			size_t e = s.find_first_of("/?#", p);
			if (e == std::string::npos) e = s.size();
			u.authority = s.substr(p, e - p);
			p = e;
		}
		size_t q = s.find('?', p);
		size_t h = s.find('#', p);
		size_t end = std::min(q == std::string::npos ? s.size() : q, h == std::string::npos ? s.size() : h);
		u.path = s.substr(p, end - p);
		if (q != std::string::npos && (h == std::string::npos || q < h))
		{
			u.hasQuery = true;
			u.query = s.substr(q + 1, (h == std::string::npos ? s.size() : h) - q - 1);
		}
		return u;
	}

	// RFC 3986's algorithm, so "//" in a base path survives and a trailing slash is kept.
	std::string removeDotSegments(std::string input)
	{
		std::string output;
		while (!input.empty())
		{
			if (input.compare(0, 3, "../") == 0) input.erase(0, 3);
			else if (input.compare(0, 2, "./") == 0) input.erase(0, 2);
			else if (input.compare(0, 3, "/./") == 0) input.replace(0, 3, "/");
			else if (input == "/.") input = "/";
			else if (input.compare(0, 4, "/../") == 0 || input == "/..")
			{
				input = input.compare(0, 4, "/../") == 0 ? "/" + input.substr(4) : "/";
				size_t slash = output.rfind('/');
				if (slash == std::string::npos) output.clear();
				else output.erase(slash);
			}
			else if (input == "." || input == "..") input.clear();
			else
			{
				size_t start = input[0] == '/' ? 1 : 0;
				size_t slash = input.find('/', start);
				if (slash == std::string::npos)
				{
					output += input;
					input.clear();
				}
				else
				{
					output += input.substr(0, slash);
					input.erase(0, slash);
				}
			}
		}
		return output;
	}

	std::string mergePath(const std::string& basePath, const std::string& rel)
	{
		if (basePath.empty()) return "/" + rel;
		size_t slash = basePath.rfind('/');
		if (slash == std::string::npos) return rel;
		return basePath.substr(0, slash + 1) + rel;
	}

	std::string urlJoin(const std::string& base, const std::string& rel)
	{
		if (hasScheme(rel)) return rel;
		UrlParts b = splitUrl(base);
		UrlParts r = splitUrl(rel);
		std::string scheme = b.scheme, authority = b.authority, path, query;
		bool hasAuthority = b.hasAuthority, hasQuery = false;
		if (r.hasAuthority)
		{
			authority = r.authority;
			hasAuthority = true;
			path = removeDotSegments(r.path);
			query = r.query;
			hasQuery = r.hasQuery;
		}
		else if (r.path.empty())
		{
			path = b.path;
			if (r.hasQuery)
			{
				query = r.query;
				hasQuery = true;
			}
			else
			{
				query = b.query;
				hasQuery = b.hasQuery;
			}
		}
		else
		{
			if (r.path[0] == '/') path = removeDotSegments(r.path);
			else path = removeDotSegments(mergePath(b.path, r.path));
			query = r.query;
			hasQuery = r.hasQuery;
		}
		std::string out;
		if (!scheme.empty()) out += scheme + ":";
		if (hasAuthority) out += "//" + authority;
		out += path;
		if (hasQuery) out += "?" + query;
		return out;
	}

	bool tableToQuery(lua_State* L, int idx, std::string& out)
	{
		if (!lua_istable(L, idx)) return false;
		idx = lua_absindex(L, idx);
		std::vector<std::pair<std::string, std::string>> parts;
		lua_pushnil(L);
		while (lua_next(L, idx))
		{
			if (lua_type(L, -2) == LUA_TSTRING || lua_isnumber(L, -2))
			{
				size_t kn = 0, vn = 0;
				std::string key, val;
				lua_pushvalue(L, -2);
				const char* k = lua_tolstring(L, -1, &kn);
				key.assign(k, kn);
				lua_pop(L, 1);
				if (lua_type(L, -1) == LUA_TSTRING || lua_isnumber(L, -1) ||
					lua_type(L, -1) == LUA_TBOOLEAN)
				{
					const char* v = lua_tolstring(L, -1, &vn);
					if (v) val.assign(v, vn);
					else if (lua_type(L, -1) == LUA_TBOOLEAN) val = lua_toboolean(L, -1) ? "true" : "false";
					parts.emplace_back(urlEncode(key), urlEncode(val));
				}
			}
			lua_pop(L, 1);
		}
		std::sort(parts.begin(), parts.end());
		for (size_t i = 0; i < parts.size(); i++)
		{
			if (i) out += '&';
			out += parts[i].first;
			out += '=';
			out += parts[i].second;
		}
		return true;
	}

	bool readFixed(const std::string& s, size_t& p, int digits, int& out)
	{
		if (p + size_t(digits) > s.size()) return false;
		int v = 0;
		for (int k = 0; k < digits; k++)
		{
			char c = s[p++];
			if (c < '0' || c > '9') return false;
			v = v * 10 + (c - '0');
		}
		out = v;
		return true;
	}

	// Days from 1970-01-01 (Howard Hinnant's civil-days algorithm): timegm is not declared under a strict C++
	// standard on every libc, and this host must return the same epoch on the handheld as on a Mac.
	int64_t utcEpoch(int year, int mon, int day, int hour, int min, int sec)
	{
		int64_t y = year;
		y -= mon <= 2 ? 1 : 0;
		const int64_t era = (y >= 0 ? y : y - 399) / 400;
		const int64_t yoe = y - era * 400;
		const int64_t doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1;
		const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		return (era * 146097 + doe - 719468) * 86400 + hour * 3600 + min * 60 + sec;
	}

	bool validDateParts(int year, int mon, int day, int hour, int min, int sec)
	{
		if (mon < 1 || mon > 12 || day < 1 || hour < 0 || hour > 23 || min < 0 || min > 59 ||
			sec < 0 || sec > 60) return false;
		static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
		int maxDay = days[mon - 1];
		bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
		if (mon == 2 && leap) maxDay = 29;
		return day <= maxDay;
	}

	bool parseWithFormat(const std::string& s, const std::string& fmt, int64_t& out)
	{
		int year = 1970, mon = 1, day = 1, hour = 0, min = 0, sec = 0;
		size_t p = 0, f = 0;
		while (f < fmt.size())
		{
			char c = fmt[f++];
			if (c != '%')
			{
				if (p >= s.size() || s[p] != c) return false;
				p++;
				continue;
			}
			if (f >= fmt.size()) return false;
			char k = fmt[f++];
			int v = 0;
			if (k == '%')
			{
				if (p >= s.size() || s[p] != '%') return false;
				p++;
				continue;
			}
			if (k == 'Y')
			{
				if (!readFixed(s, p, 4, v)) return false;
				year = v;
			}
			else if (k == 'm' || k == 'd' || k == 'H' || k == 'M' || k == 'S')
			{
				if (p >= s.size() || s[p] < '0' || s[p] > '9') return false;
				v = s[p++] - '0';
				if (p < s.size() && s[p] >= '0' && s[p] <= '9') v = v * 10 + (s[p++] - '0');
				if (k == 'm') mon = v;
				else if (k == 'd') day = v;
				else if (k == 'H') hour = v;
				else if (k == 'M') min = v;
				else sec = v;
			}
			else return false;
		}
		if (p != s.size() || !validDateParts(year, mon, day, hour, min, sec)) return false;
		out = utcEpoch(year, mon, day, hour, min, sec);
		return true;
	}

	bool parseIsoDate(const std::string& s, int64_t& out)
	{
		size_t p = 0;
		int year = 0, mon = 0, day = 0, hour = 0, min = 0, sec = 0;
		if (!readFixed(s, p, 4, year) || p >= s.size() || s[p++] != '-') return false;
		if (!readFixed(s, p, 2, mon) || p >= s.size() || s[p++] != '-') return false;
		if (!readFixed(s, p, 2, day)) return false;
		if (p < s.size() && (s[p] == 'T' || s[p] == ' '))
		{
			p++;
			if (!readFixed(s, p, 2, hour) || p >= s.size() || s[p++] != ':') return false;
			if (!readFixed(s, p, 2, min) || p >= s.size() || s[p++] != ':') return false;
			if (!readFixed(s, p, 2, sec)) return false;
			if (p < s.size() && s[p] == '.')
			{
				p++;
				if (p >= s.size() || s[p] < '0' || s[p] > '9') return false;
				while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
			}
		}
		int64_t offset = 0;
		if (p < s.size() && (s[p] == 'Z' || s[p] == 'z')) p++;
		else if (p < s.size() && (s[p] == '+' || s[p] == '-'))
		{
			int sign = s[p++] == '-' ? -1 : 1;
			int oh = 0, om = 0;
			if (!readFixed(s, p, 2, oh) || p >= s.size() || s[p++] != ':') return false;
			if (!readFixed(s, p, 2, om)) return false;
			offset = sign * (oh * 3600 + om * 60);
		}
		if (p != s.size() || !validDateParts(year, mon, day, hour, min, sec)) return false;
		out = utcEpoch(year, mon, day, hour, min, sec) - offset;
		return true;
	}

	bool parseDate(const std::string& s, const std::string* fmt, int64_t& out)
	{
		if (fmt) return parseWithFormat(s, *fmt, out);
		return parseIsoDate(s, out);
	}

	struct FilterBudget
	{
		int filters = 0, options = 0;
	};

	bool normalizeFilterList(const JVal& in, JVal& out, FilterBudget& budget, std::string& error)
	{
		if (in.type != JVal::Arr)
		{
			error = "filters() must return a list";
			return false;
		}
		static const char* const types[] = {"select", "sort", "check", "multi", "tri", "text", "group"};
		out.type = JVal::Arr;
		for (const JVal& item : in.a)
		{
			if (item.type != JVal::Obj) continue;
			if (budget.filters >= 50) break;
			const JVal* id = item.find("id");
			const JVal* type = item.find("type");
			if (!id || id->type != JVal::Str || id->s.empty()) continue;
			if (!type || type->type != JVal::Str) continue;
			bool known = false;
			for (const char* t : types)
				if (type->s == t) known = true;
			if (!known) continue;
			budget.filters++;
			JVal f;
			f.type = JVal::Obj;
			for (const auto& kv : item.o)
			{
				if (kv.first == "id" || kv.first == "type" || kv.first == "name" ||
					kv.first == "options" || kv.first == "filters") continue;
				f.o.push_back(kv);
			}
			JVal v;
			v.type = JVal::Str;
			v.s = id->s;
			f.o.emplace_back("id", v);
			v.s = type->s;
			f.o.emplace_back("type", v);
			std::string label = id->s;
			const JVal* nm = item.find("name");
			if (nm && nm->type == JVal::Str && !nm->s.empty()) label = nm->s;
			v.s = clipUtf8(label, 60);
			f.o.emplace_back("name", v);
			if (type->s == "group")
			{
				const JVal* kids = item.find("filters");
				if (kids && kids->type == JVal::Arr)
				{
					JVal normKids;
					if (!normalizeFilterList(*kids, normKids, budget, error)) return false;
					f.o.emplace_back("filters", std::move(normKids));
				}
			}
			else
			{
				const JVal* opts = item.find("options");
				if (opts && opts->type == JVal::Arr)
				{
					JVal normOpts;
					normOpts.type = JVal::Arr;
					for (const JVal& o : opts->a)
					{
						if (budget.options >= 2000) break;
						std::string oid, oname;
						if (o.type == JVal::Str)
						{
							oid = o.s;
							oname = o.s;
						}
						else if (o.type == JVal::Obj)
						{
							const JVal* oi = o.find("id");
							if (!oi || oi->type != JVal::Str || oi->s.empty()) continue;
							oid = oi->s;
							oname = oid;
							const JVal* on = o.find("name");
							if (on && on->type == JVal::Str && !on->s.empty()) oname = on->s;
						}
						else continue;
						budget.options++;
						JVal opt;
						opt.type = JVal::Obj;
						JVal ov;
						ov.type = JVal::Str;
						ov.s = oid;
						opt.o.emplace_back("id", ov);
						ov.s = clipUtf8(oname, 60);
						opt.o.emplace_back("name", ov);
						normOpts.a.push_back(std::move(opt));
					}
					f.o.emplace_back("options", std::move(normOpts));
				}
			}
			out.a.push_back(std::move(f));
		}
		return true;
	}
}

namespace bp
{
	// The addresses are registry keys; their value is irrelevant.
	static char ctxKey;

	static int bpHttpGet(lua_State* L);
	static int bpHttpPost(lua_State* L);
	static int bpJsonDecode(lua_State* L);
	static int bpJsonEncode(lua_State* L);
	static int bpUrlEncode(lua_State* L);
	static int bpUrlDecode(lua_State* L);
	static int bpUrlJoin(lua_State* L);
	static int bpUrlQuery(lua_State* L);
	static int bpPrefsGet(lua_State* L);
	static int bpPrefsSet(lua_State* L);
	static int bpLogFn(lua_State* L);
	static int bpTimeFn(lua_State* L);
	static int bpDateFn(lua_State* L);
	static void htmlInstall(lua_State* L);
	static int bpHtmlParse(lua_State* L);
	static int bpCollectFn(lua_State* L);
	static int coCreate(lua_State* L);
	static int coWrap(lua_State* L);
	static int coAuxWrap(lua_State* L);
	static void budgetHook(lua_State* L, lua_Debug* ar);

	struct SourceHost::Impl
	{
		std::string script, expectedId;
		SourceMeta meta;
		lua_State* L = nullptr;
		int sourceRef = LUA_NOREF;
		HttpFn http;
		std::string prefsPath;
		std::map<std::string, std::string> prefs;
		bool prefsLoaded = false;
		// the design's limits (docs/design/manga.md 5.4): 30 s a call, 32 MB a source
		double wallSeconds = 30.0;
		uint64_t instructionBudget = 200000000;
		size_t memoryCap = 32u * 1024u * 1024u;
		MemCtx mem;
		bool dirty = false;
		int requestsThisCall = 0;
		std::chrono::steady_clock::time_point logWindow;
		int logLines = 0;

		struct Ctx
		{
			bool active = false, tripped = false, byTime = false;
			uint64_t instructions = 0, budget = 0;
			std::chrono::steady_clock::time_point deadline;
		} ctx;

		~Impl() { closeState(); }
		void closeState()
		{
			if (L)
			{
				lua_close(L);
				L = nullptr;
			}
			sourceRef = LUA_NOREF;
			ctx.active = false;
		}
		bool createState(std::string& error);
		void installSandbox();
		void installBp();
		bool installSource(std::string& error);
		bool ensureState(std::string& error);
		bool prepare(std::string& error);
		bool pushFunction(const char* name, bool required, std::string& error);
		bool callFunction(const char* name, int nargs, std::string& error);
		bool readMeta(lua_State* L, int idx, std::string& error);
		int httpCall(lua_State* L, const char* method);
		void loadPrefs();
		bool savePrefs(std::string& error);
		size_t prefsBytes() const;
		bool logAllowed();
		bool readList(lua_State* L, int idx, MangaList& out, std::string& error);
		bool readDetails(lua_State* L, int idx, MangaDetails& out, std::string& error);
		bool readChapters(lua_State* L, int idx, std::vector<MangaChapter>& out, std::string& error);
		bool readPages(lua_State* L, int idx, std::vector<MangaPage>& out, std::string& error);
		bool readImage(lua_State* L, int idx, MangaImage& out, std::string& error);
	};

	using Impl = SourceHost::Impl;

	static Impl* implOf(lua_State* L)
	{
		lua_rawgetp(L, LUA_REGISTRYINDEX, &ctxKey);
		Impl* im = static_cast<Impl*>(lua_touserdata(L, -1));
		lua_pop(L, 1);
		return im;
	}

	// Throw a Lua error from a hook: luaL_error adds position info and unwinds to the pending pcall.
	static void budgetHook(lua_State* L, lua_Debug*)
	{
		Impl* im = implOf(L);
		if (!im || !im->ctx.active) return;
		im->ctx.instructions += kHookInstructions;
		if (im->ctx.instructions > im->ctx.budget)
		{
			im->ctx.tripped = true;
			im->ctx.byTime = false;
			luaL_error(L, "source exceeded its instruction budget");
		}
		if (std::chrono::steady_clock::now() > im->ctx.deadline)
		{
			im->ctx.tripped = true;
			im->ctx.byTime = true;
			luaL_error(L, "source exceeded its time limit");
		}
	}

	bool SourceHost::Impl::createState(std::string& error)
	{
		mem.used = 0;
		mem.cap = memoryCap;
		L = lua_newstate(cappedAlloc, &mem);
		if (!L)
		{
			error = "out of memory: the source needs more than its memory limit";
			return false;
		}
		lua_pushlightuserdata(L, this);
		lua_rawsetp(L, LUA_REGISTRYINDEX, &ctxKey);

		luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
		lua_pop(L, 1);
		luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
		lua_pop(L, 1);
		luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
		lua_pop(L, 1);
		luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
		lua_pop(L, 1);
		luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
		lua_pop(L, 1);
		luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
		lua_pop(L, 1);

		installSandbox();
		lua_sethook(L, budgetHook, LUA_MASKCOUNT, kHookInstructions);
		return true;
	}

	void SourceHost::Impl::installSandbox()
	{
		static const char* const removed[] = {"io", "os", "package", "require", "debug", "load", "loadfile",
			"dofile", "loadstring"};
		for (const char* name : removed)
		{
			lua_pushnil(L);
			lua_setglobal(L, name);
		}
		lua_getglobal(L, LUA_STRLIBNAME);
		lua_pushnil(L);
		lua_setfield(L, -2, "dump");
		lua_pop(L, 1);

		lua_pushlightuserdata(L, this);
		lua_pushcclosure(L, bpLogFn, 1);
		lua_setglobal(L, "print");
		lua_pushcfunction(L, bpCollectFn);
		lua_setglobal(L, "collectgarbage");

		// Every coroutine gets the same count hook; the stock create/wrap don't copy one.
		lua_getglobal(L, LUA_COLIBNAME);
		lua_pushcfunction(L, coCreate);
		lua_setfield(L, -2, "create");
		lua_pushcfunction(L, coWrap);
		lua_setfield(L, -2, "wrap");
		lua_pop(L, 1);

		installBp();
	}

	void SourceHost::Impl::installBp()
	{
		lua_newtable(L);	// bp

		lua_newtable(L);	// bp.http
		lua_pushlightuserdata(L, this);
		lua_pushcclosure(L, bpHttpGet, 1);
		lua_setfield(L, -2, "get");
		lua_pushlightuserdata(L, this);
		lua_pushcclosure(L, bpHttpPost, 1);
		lua_setfield(L, -2, "post");
		lua_setfield(L, -2, "http");

		lua_newtable(L);	// bp.json
		lua_pushcfunction(L, bpJsonDecode);
		lua_setfield(L, -2, "decode");
		lua_pushcfunction(L, bpJsonEncode);
		lua_setfield(L, -2, "encode");
		lua_setfield(L, -2, "json");

		lua_newtable(L);	// bp.url
		lua_pushcfunction(L, bpUrlEncode);
		lua_setfield(L, -2, "encode");
		lua_pushcfunction(L, bpUrlDecode);
		lua_setfield(L, -2, "decode");
		lua_pushcfunction(L, bpUrlJoin);
		lua_setfield(L, -2, "join");
		lua_pushcfunction(L, bpUrlQuery);
		lua_setfield(L, -2, "query");
		lua_setfield(L, -2, "url");

		lua_newtable(L);	// bp.prefs
		lua_pushlightuserdata(L, this);
		lua_pushcclosure(L, bpPrefsGet, 1);
		lua_setfield(L, -2, "get");
		lua_pushlightuserdata(L, this);
		lua_pushcclosure(L, bpPrefsSet, 1);
		lua_setfield(L, -2, "set");
		lua_setfield(L, -2, "prefs");

		lua_pushlightuserdata(L, this);
		lua_pushcclosure(L, bpLogFn, 1);
		lua_setfield(L, -2, "log");

		lua_pushcfunction(L, bpTimeFn);
		lua_setfield(L, -2, "time");
		lua_pushcfunction(L, bpDateFn);
		lua_setfield(L, -2, "date");

		lua_newtable(L);	// bp.html (design 5.2): parse hands back a document, documents and elements elements
		htmlInstall(L);		// the two metatables, so an element can hold its document open past the doc variable
		lua_pushlightuserdata(L, &mem);	// parse charges the tree against this ledger: it is not Lua's allocator's
		lua_pushcclosure(L, bpHtmlParse, 1);
		lua_setfield(L, -2, "parse");
		lua_setfield(L, -2, "html");
		lua_setglobal(L, "bp");
	}

	static std::string luaErrorMessage(lua_State* L, int idx)
	{
		if (lua_type(L, idx) == LUA_TSTRING || lua_isnumber(L, idx))
		{
			size_t n = 0;
			const char* s = lua_tolstring(L, idx, &n);
			return std::string(s, n);
		}
		return "unknown Lua error";
	}

	bool SourceHost::Impl::installSource(std::string& error)
	{
		int base = lua_gettop(L);
		std::string chunk = "@" + (expectedId.empty() ? std::string("source") : expectedId) + ".lua";
		int rc = luaL_loadbufferx(L, script.data(), script.size(), chunk.c_str(), "t");
		if (rc != LUA_OK)
		{
			std::string msg = luaErrorMessage(L, -1);
			lua_settop(L, base);
			error = "can't load source: " + msg;
			return false;
		}
		ctx.active = true;
		ctx.instructions = 0;
		ctx.budget = instructionBudget;
		ctx.tripped = false;
		ctx.byTime = false;
		requestsThisCall = 0;
		meta.id = expectedId;	// so a bp.log from the script's top level already has the right prefix
		ctx.deadline = std::chrono::steady_clock::now() +
			std::chrono::milliseconds((int64_t)(wallSeconds * 1000.0));
		rc = lua_pcall(L, 0, 1, 0);
		ctx.active = false;
		if (rc != LUA_OK)
		{
			std::string msg = luaErrorMessage(L, -1);
			lua_settop(L, base);
			if (ctx.tripped) error = ctx.byTime ? "source took too long to load" : "source used too many instructions to load";
			else if (rc == LUA_ERRMEM) error = "source ran out of memory while loading";
			else error = msg;
			return false;
		}
		if (!lua_istable(L, -1))
		{
			lua_settop(L, base);
			error = "the source must return a table";
			return false;
		}
		if (!readMeta(L, -1, error))
		{
			lua_settop(L, base);
			return false;
		}
		lua_pushvalue(L, -1);
		sourceRef = luaL_ref(L, LUA_REGISTRYINDEX);
		lua_settop(L, base);
		return true;
	}

	bool SourceHost::Impl::readMeta(lua_State* L, int idx, std::string& error)
	{
		idx = lua_absindex(L, idx);
		meta = SourceMeta{};
		lua_getfield(L, idx, "api");
		if (!lua_isnumber(L, -1))
		{
			lua_pop(L, 1);
			error = "the source has no api";
			return false;
		}
		double api = lua_tonumber(L, -1);
		lua_pop(L, 1);
		if (api > 1.0)
		{
			error = "needs a newer Banana Player";
			return false;
		}
		if (api != 1.0)
		{
			error = "the source's api isn't supported";
			return false;
		}
		std::string id;
		if (!getStringField(L, idx, "id", id, true, error)) return false;
		if (id != expectedId)
		{
			error = "the source's id \"" + id + "\" doesn't match \"" + expectedId + "\"";
			return false;
		}
		meta.id = id;
		if (!getStringField(L, idx, "name", meta.name, true, error)) return false;
		if (meta.name.empty())
		{
			error = "the source has no name";
			return false;
		}
		std::string ignored;
		getStringField(L, idx, "lang", meta.lang, false, ignored);
		getStringField(L, idx, "version", meta.version, false, ignored);
		getStringField(L, idx, "baseUrl", meta.baseUrl, false, ignored);
		getBoolField(L, idx, "nsfw", meta.nsfw);
		lua_getfield(L, idx, "headers");
		readHeaders(L, -1, meta.headers);
		lua_pop(L, 1);

		lua_getfield(L, idx, "prefs");
		if (lua_istable(L, -1))
		{
			size_t n = lua_rawlen(L, -1);
			if (n > 50) n = 50;
			for (size_t i = 1; i <= n; i++)
			{
				lua_rawgeti(L, -1, (lua_Integer)i);
				if (lua_istable(L, -1))
				{
					SourcePref p;
					std::string dummy;
					getStringField(L, -1, "id", p.id, false, dummy);
					getStringField(L, -1, "type", p.type, false, dummy);
					getStringField(L, -1, "name", p.name, false, dummy);
					lua_getfield(L, -1, "default");
					if (lua_type(L, -1) == LUA_TSTRING || lua_isnumber(L, -1))
					{
						size_t vn = 0;
						const char* v = lua_tolstring(L, -1, &vn);
						p.def.assign(v, vn);
					}
					else if (!lua_isnil(L, -1))
					{
						JVal v;
						std::string jerr;
						std::set<const void*> seen;
						if (luaToJson(L, -1, v, jerr, 0, seen)) encodeJson(v, p.def);
					}
					lua_pop(L, 1);
					lua_getfield(L, -1, "options");
					if (lua_istable(L, -1))
					{
						size_t on = lua_rawlen(L, -1);
						if (on > 2000) on = 2000;
						for (size_t k = 1; k <= on; k++)
						{
							lua_rawgeti(L, -1, (lua_Integer)k);
							std::string label;
							if (lua_type(L, -1) == LUA_TSTRING || lua_isnumber(L, -1))
							{
								size_t vn = 0;
								const char* v = lua_tolstring(L, -1, &vn);
								label.assign(v, vn);
							}
							else if (lua_istable(L, -1))
							{
								std::string oid, oname;
								getStringField(L, -1, "id", oid, false, dummy);
								if (getStringField(L, -1, "name", oname, false, dummy) && !oname.empty())
									label = oname;
								else label = oid;
							}
							lua_pop(L, 1);
							if (!label.empty()) p.options.push_back(clipUtf8(label, 60));
						}
					}
					lua_pop(L, 1);
					if (!p.id.empty()) meta.prefs.push_back(std::move(p));
				}
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);

		static const char* const required[] = {"popular", "details", "chapters", "pages"};
		for (const char* fn : required)
		{
			lua_getfield(L, idx, fn);
			bool ok = lua_isfunction(L, -1);
			lua_pop(L, 1);
			if (!ok)
			{
				error = std::string("the source has no ") + fn + " function";
				return false;
			}
		}
		return true;
	}

	bool SourceHost::Impl::ensureState(std::string& error)
	{
		if (L) return true;
		if (script.empty())
		{
			error = "no source is loaded";
			return false;
		}
		if (!createState(error)) return false;
		if (!installSource(error))
		{
			closeState();
			return false;
		}
		return true;
	}

	bool SourceHost::Impl::prepare(std::string& error)
	{
		if (dirty)
		{
			// After a memory or limit error the state isn't trustworthy; a fresh one keeps the next call alive.
			closeState();
			dirty = false;
		}
		return ensureState(error);
	}

	bool SourceHost::Impl::pushFunction(const char* name, bool required, std::string& error)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, sourceRef);
		lua_getfield(L, -1, name);
		lua_remove(L, -2);
		if (!lua_isfunction(L, -1))
		{
			lua_pop(L, 1);
			if (required) error = std::string("the source has no ") + name + " function";
			return false;
		}
		return true;
	}

	bool SourceHost::Impl::callFunction(const char* name, int nargs, std::string& error)
	{
		ctx.active = true;
		ctx.instructions = 0;
		ctx.budget = instructionBudget;
		ctx.tripped = false;
		ctx.byTime = false;
		requestsThisCall = 0;
		ctx.deadline = std::chrono::steady_clock::now() +
			std::chrono::milliseconds((int64_t)(wallSeconds * 1000.0));
		int rc = lua_pcall(L, nargs, 1, 0);
		ctx.active = false;
		if (rc != LUA_OK)
		{
			std::string msg = luaErrorMessage(L, -1);
			lua_pop(L, 1);
			if (ctx.tripped)
			{
				error = std::string(name) + ": " +
					(ctx.byTime ? "the source exceeded its time limit" : "the source exceeded its instruction budget");
				dirty = true;
			}
			else if (rc == LUA_ERRMEM)
			{
				error = std::string(name) + ": the source ran out of memory";
				dirty = true;
			}
			else error = std::string(name) + ": " + msg;
			return false;
		}
		return true;
	}

	int SourceHost::Impl::httpCall(lua_State* L, const char* method)
	{
		if (!http) return luaL_error(L, "bp.http: no network handler is installed");
		if (++requestsThisCall > kMaxRequestsPerCall)
			return luaL_error(L, "bp.http: more than %d requests in one call", kMaxRequestsPerCall);
		const char* u = luaL_checkstring(L, 1);
		std::string url(u);
		std::string body;
		int opt = 2;
		if (std::strcmp(method, "POST") == 0)
		{
			if (!lua_isnoneornil(L, 2))
			{
				size_t n = 0;
				const char* b = luaL_checklstring(L, 2, &n);
				body.assign(b, n);
			}
			opt = 3;
		}
		std::map<std::string, std::string> headers = meta.headers;
		int timeout = 20;
		if (lua_istable(L, opt))
		{
			lua_getfield(L, opt, "headers");
			readHeaders(L, -1, headers);
			lua_pop(L, 1);
			lua_getfield(L, opt, "timeout");
			if (lua_isnumber(L, -1))
			{
				double t = lua_tonumber(L, -1);
				if (t > 0) timeout = (int)(t > 300 ? 300 : (t < 1 ? 1 : t));
			}
			lua_pop(L, 1);
			lua_getfield(L, opt, "form");
			if (lua_istable(L, -1))
			{
				std::string form;
				if (tableToQuery(L, -1, form)) body = form;
				bool hasType = false;
				for (const auto& kv : headers)
					if (lower(kv.first) == "content-type") hasType = true;
				if (!hasType) headers["Content-Type"] = "application/x-www-form-urlencoded";
			}
			lua_pop(L, 1);
		}
		HttpResult r;
		try
		{
			r = http(method, url, headers, body, timeout);
		}
		catch (const std::exception& e)
		{
			return luaL_error(L, "bp.http: %s", e.what());
		}
		catch (...)
		{
			return luaL_error(L, "bp.http: the request failed");
		}
		if (r.body.size() > kMaxResponse)
			return luaL_error(L, "bp.http: the response is larger than 8 MB");
		if (ctx.active && std::chrono::steady_clock::now() > ctx.deadline)
		{
			ctx.tripped = true;
			ctx.byTime = true;
			return luaL_error(L, "source exceeded its time limit");
		}
		lua_createtable(L, 0, 4);
		lua_pushinteger(L, r.status);
		lua_setfield(L, -2, "status");
		lua_pushlstring(L, r.body.data(), r.body.size());
		lua_setfield(L, -2, "body");
		lua_pushlstring(L, r.url.empty() ? url.data() : r.url.data(), r.url.empty() ? url.size() : r.url.size());
		lua_setfield(L, -2, "url");
		lua_newtable(L);
		for (const auto& kv : r.headers)
		{
			lua_pushlstring(L, kv.second.data(), kv.second.size());
			lua_setfield(L, -2, kv.first.c_str());
		}
		lua_setfield(L, -2, "headers");
		return 1;
	}

	bool SourceHost::Impl::readList(lua_State* L, int idx, MangaList& out, std::string& error)
	{
		idx = lua_absindex(L, idx);
		out = MangaList{};
		if (!lua_istable(L, idx))
		{
			error = "the source returned no list";
			return false;
		}
		lua_getfield(L, idx, "more");
		out.more = lua_toboolean(L, -1) != 0;
		lua_pop(L, 1);
		lua_getfield(L, idx, "items");
		if (lua_isnil(L, -1))
		{
			lua_pop(L, 1);
			return true;
		}
		if (!lua_istable(L, -1))
		{
			lua_pop(L, 1);
			error = "the source's items isn't a list";
			return false;
		}
		size_t n = lua_rawlen(L, -1);
		for (size_t i = 1; i <= n && out.items.size() < kMaxList; i++)
		{
			lua_rawgeti(L, -1, (lua_Integer)i);
			if (lua_istable(L, -1))
			{
				MangaItem item;
				std::string dummy;
				if (getStringField(L, -1, "key", item.key, true, dummy) && !item.key.empty())
				{
					std::string t;
					getStringField(L, -1, "title", item.title, false, dummy);
					getStringField(L, -1, "cover", item.cover, false, dummy);
					item.key = clipBytes(item.key, kMaxString);
					item.title = clipBytes(item.title, kMaxString);
					item.cover = clipBytes(item.cover, kMaxString);
					JVal v;
					std::string jerr;
					std::set<const void*> seen;
					if (luaToJson(L, -1, v, jerr, 0, seen)) encodeJson(v, item.json);
					out.items.push_back(std::move(item));
				}
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		return true;
	}

	bool SourceHost::Impl::readDetails(lua_State* L, int idx, MangaDetails& out, std::string& error)
	{
		idx = lua_absindex(L, idx);
		out = MangaDetails{};
		if (!lua_istable(L, idx))
		{
			error = "the source returned no details";
			return false;
		}
		std::string ignored;
		getStringField(L, idx, "title", out.title, false, ignored);
		getStringField(L, idx, "author", out.author, false, ignored);
		getStringField(L, idx, "artist", out.artist, false, ignored);
		getStringField(L, idx, "description", out.description, false, ignored);
		getStringField(L, idx, "cover", out.cover, false, ignored);
		out.title = clipBytes(out.title, kMaxString);
		out.author = clipBytes(out.author, kMaxString);
		out.artist = clipBytes(out.artist, kMaxString);
		out.description = clipBytes(out.description, kMaxString);
		out.cover = clipBytes(out.cover, kMaxString);
		std::string status;
		getStringField(L, idx, "status", status, false, ignored);
		status = lower(status);
		if (status == "ongoing" || status == "completed" || status == "hiatus" || status == "cancelled" ||
			status == "unknown") out.status = status;
		else out.status = "unknown";
		lua_getfield(L, idx, "genres");
		if (lua_istable(L, -1))
		{
			size_t n = lua_rawlen(L, -1);
			if (n > kMaxList) n = kMaxList;
			for (size_t i = 1; i <= n; i++)
			{
				lua_rawgeti(L, -1, (lua_Integer)i);
				if (lua_type(L, -1) == LUA_TSTRING || lua_isnumber(L, -1))
				{
					size_t vn = 0;
					const char* v = lua_tolstring(L, -1, &vn);
					out.genres.push_back(clipBytes(std::string(v, vn), kMaxString));
				}
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
		return true;
	}

	bool SourceHost::Impl::readChapters(lua_State* L, int idx, std::vector<MangaChapter>& out,
		std::string& error)
	{
		idx = lua_absindex(L, idx);
		out.clear();
		if (!lua_istable(L, idx))
		{
			error = "the source returned no chapters";
			return false;
		}
		size_t n = lua_rawlen(L, idx);
		for (size_t i = 1; i <= n && out.size() < kMaxList; i++)
		{
			lua_rawgeti(L, idx, (lua_Integer)i);
			if (lua_istable(L, -1))
			{
				MangaChapter c;
				std::string dummy;
				if (getStringField(L, -1, "key", c.key, true, dummy) && !c.key.empty())
				{
					getStringField(L, -1, "name", c.name, false, dummy);
					getStringField(L, -1, "group", c.group, false, dummy);
					lua_getfield(L, -1, "number");
					if (lua_isnumber(L, -1)) c.number = lua_tonumber(L, -1);
					lua_pop(L, 1);
					lua_getfield(L, -1, "date");
					if (lua_isnumber(L, -1)) c.date = (int64_t)lua_tonumber(L, -1);
					lua_pop(L, 1);
					c.key = clipBytes(c.key, kMaxString);
					c.name = clipBytes(c.name, kMaxString);
					c.group = clipBytes(c.group, kMaxString);
					JVal v;
					std::string jerr;
					std::set<const void*> seen;
					if (luaToJson(L, -1, v, jerr, 0, seen)) encodeJson(v, c.json);
					out.push_back(std::move(c));
				}
			}
			lua_pop(L, 1);
		}
		return true;
	}

	bool SourceHost::Impl::readPages(lua_State* L, int idx, std::vector<MangaPage>& out, std::string& error)
	{
		idx = lua_absindex(L, idx);
		out.clear();
		if (!lua_istable(L, idx))
		{
			error = "the source returned no pages";
			return false;
		}
		size_t n = lua_rawlen(L, idx);
		for (size_t i = 1; i <= n && out.size() < kMaxList; i++)
		{
			lua_rawgeti(L, idx, (lua_Integer)i);
			if (lua_istable(L, -1))
			{
				MangaPage p;
				std::string dummy;
				getStringField(L, -1, "url", p.url, false, dummy);
				getStringField(L, -1, "key", p.key, false, dummy);
				p.url = clipBytes(p.url, kMaxString);
				p.key = clipBytes(p.key, kMaxString);
				if (!p.url.empty() || !p.key.empty())
				{
					JVal v;
					std::string jerr;
					std::set<const void*> seen;
					if (luaToJson(L, -1, v, jerr, 0, seen)) encodeJson(v, p.json);
					out.push_back(std::move(p));
				}
			}
			lua_pop(L, 1);
		}
		return true;
	}

	bool SourceHost::Impl::readImage(lua_State* L, int idx, MangaImage& out, std::string& error)
	{
		idx = lua_absindex(L, idx);
		out = MangaImage{};
		if (!lua_istable(L, idx))
		{
			error = "the source returned no image";
			return false;
		}
		if (!getStringField(L, idx, "url", out.url, true, error)) return false;
		out.url = clipBytes(out.url, kMaxString);
		out.headers = meta.headers;
		lua_getfield(L, idx, "headers");
		readHeaders(L, -1, out.headers);
		lua_pop(L, 1);
		return true;
	}

	size_t SourceHost::Impl::prefsBytes() const
	{
		size_t total = 0;
		for (const auto& kv : prefs) total += kv.first.size() + kv.second.size() + 4;
		return total;
	}

	void SourceHost::Impl::loadPrefs()
	{
		prefs.clear();
		prefsLoaded = true;
		if (prefsPath.empty()) return;
		std::ifstream in(prefsPath, std::ios::binary);
		if (!in) return;
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		JVal v;
		std::string err;
		if (!parseJson(text, v, err) || v.type != JVal::Obj) return;
		for (const auto& kv : v.o)
		{
			if (kv.second.type != JVal::Str) continue;
			if (prefsBytes() + kv.first.size() + kv.second.s.size() + 4 > kMaxPrefs) break;
			prefs[kv.first] = kv.second.s;
		}
	}

	bool SourceHost::Impl::savePrefs(std::string& error)
	{
		if (prefsPath.empty()) return true;
		JVal v;
		v.type = JVal::Obj;
		for (const auto& kv : prefs)
		{
			JVal s;
			s.type = JVal::Str;
			s.s = kv.second;
			v.o.emplace_back(kv.first, std::move(s));
		}
		std::string text;
		encodeJson(v, text);
		std::string tmp = prefsPath + ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out)
			{
				error = "can't write " + tmp;
				return false;
			}
			out.write(text.data(), (std::streamsize)text.size());
			if (!out)
			{
				error = "can't write " + tmp;
				return false;
			}
		}
		if (std::rename(tmp.c_str(), prefsPath.c_str()) != 0)
		{
			error = "can't move the prefs file into place";
			return false;
		}
		return true;
	}

	bool SourceHost::Impl::logAllowed()
	{
		auto now = std::chrono::steady_clock::now();
		if (now - logWindow >= std::chrono::seconds(1))
		{
			logWindow = now;
			logLines = 0;
		}
		if (logLines >= 20) return false;
		logLines++;
		return true;
	}

	// ---- the bp API ----

	static int bpHttpGet(lua_State* L)
	{
		Impl* im = static_cast<Impl*>(lua_touserdata(L, lua_upvalueindex(1)));
		return im->httpCall(L, "GET");
	}

	static int bpHttpPost(lua_State* L)
	{
		Impl* im = static_cast<Impl*>(lua_touserdata(L, lua_upvalueindex(1)));
		return im->httpCall(L, "POST");
	}

	static int bpJsonDecode(lua_State* L)
	{
		size_t n = 0;
		const char* s = luaL_checklstring(L, 1, &n);
		JVal v;
		std::string err;
		if (!parseJson(std::string(s, n), v, err)) return luaL_error(L, "bp.json.decode: %s", err.c_str());
		if (!pushJson(L, v, 0)) return luaL_error(L, "bp.json.decode: nested too deeply");
		return 1;
	}

	static int bpJsonEncode(lua_State* L)
	{
		JVal v;
		std::string err;
		std::set<const void*> seen;
		if (!luaToJson(L, 1, v, err, 0, seen)) return luaL_error(L, "bp.json.encode: %s", err.c_str());
		std::string out;
		encodeJson(v, out);
		lua_pushlstring(L, out.data(), out.size());
		return 1;
	}

	static int bpUrlEncode(lua_State* L)
	{
		size_t n = 0;
		const char* s = luaL_checklstring(L, 1, &n);
		std::string out = urlEncode(std::string(s, n));
		lua_pushlstring(L, out.data(), out.size());
		return 1;
	}

	static int bpUrlDecode(lua_State* L)
	{
		size_t n = 0;
		const char* s = luaL_checklstring(L, 1, &n);
		std::string out = urlDecode(std::string(s, n));
		lua_pushlstring(L, out.data(), out.size());
		return 1;
	}

	static int bpUrlJoin(lua_State* L)
	{
		size_t bn = 0, rn = 0;
		const char* b = luaL_checklstring(L, 1, &bn);
		const char* r = luaL_checklstring(L, 2, &rn);
		std::string out = urlJoin(std::string(b, bn), std::string(r, rn));
		lua_pushlstring(L, out.data(), out.size());
		return 1;
	}

	static int bpUrlQuery(lua_State* L)
	{
		luaL_checktype(L, 1, LUA_TTABLE);
		std::string out;
		tableToQuery(L, 1, out);
		lua_pushlstring(L, out.data(), out.size());
		return 1;
	}

	static int bpPrefsGet(lua_State* L)
	{
		Impl* im = static_cast<Impl*>(lua_touserdata(L, lua_upvalueindex(1)));
		const char* id = luaL_checkstring(L, 1);
		auto it = im->prefs.find(id);
		if (it == im->prefs.end()) lua_pushnil(L);
		else lua_pushlstring(L, it->second.data(), it->second.size());
		return 1;
	}

	static int bpPrefsSet(lua_State* L)
	{
		Impl* im = static_cast<Impl*>(lua_touserdata(L, lua_upvalueindex(1)));
		const char* id = luaL_checkstring(L, 1);
		if (lua_isnoneornil(L, 2))
		{
			im->prefs.erase(id);
		}
		else
		{
			size_t n = 0;
			const char* value = luaL_checklstring(L, 2, &n);
			size_t old = 0;
			auto it = im->prefs.find(id);
			if (it != im->prefs.end()) old = it->second.size();
			if (im->prefsBytes() - (it != im->prefs.end() ? old + it->first.size() + 4 : 0) +
				std::strlen(id) + n + 4 > kMaxPrefs)
				return luaL_error(L, "bp.prefs: this source's prefs would exceed 64 KB");
			im->prefs[id] = std::string(value, n);
		}
		std::string err;
		if (!im->savePrefs(err)) return luaL_error(L, "bp.prefs: %s", err.c_str());
		lua_pushboolean(L, 1);
		return 1;
	}

	static int bpLogFn(lua_State* L)
	{
		Impl* im = static_cast<Impl*>(lua_touserdata(L, lua_upvalueindex(1)));
		int n = lua_gettop(L);
		std::string line;
		for (int i = 1; i <= n; i++)
		{
			if (i > 1) line += '\t';
			size_t len = 0;
			const char* s = luaL_tolstring(L, i, &len);
			if (s) line.append(s, len);
			lua_pop(L, 1);
		}
		if (im->logAllowed())
			std::fprintf(stderr, "[%s] %s\n", im->meta.id.empty() ? "source" : im->meta.id.c_str(), line.c_str());
		return 0;
	}

	static int bpTimeFn(lua_State* L)
	{
		lua_pushinteger(L, (lua_Integer)std::time(nullptr));
		return 1;
	}

	static int bpDateFn(lua_State* L)
	{
		size_t n = 0;
		const char* s = luaL_checklstring(L, 1, &n);
		std::string fmt;
		bool hasFmt = !lua_isnoneornil(L, 2);
		if (hasFmt)
		{
			size_t fn = 0;
			const char* f = luaL_checklstring(L, 2, &fn);
			fmt.assign(f, fn);
		}
		int64_t t = 0;
		if (parseDate(std::string(s, n), hasFmt ? &fmt : nullptr, t)) lua_pushinteger(L, (lua_Integer)t);
		else lua_pushnil(L);
		return 1;
	}

	static int bpCollectFn(lua_State* L)
	{
		const char* opt = luaL_optstring(L, 1, "collect");
		if (std::strcmp(opt, "count") == 0)
		{
			int kb = lua_gc(L, LUA_GCCOUNT);
			int b = lua_gc(L, LUA_GCCOUNTB);
			lua_pushnumber(L, kb + b / 1024.0);
			return 1;
		}
		return luaL_error(L, "collectgarbage(%s) isn't available in a source", opt);
	}

	// ---- bp.html ----
	//
	// A document is an HtmlDoc behind a shared_ptr that the document userdata and every element userdata of it
	// hold, so an element stays readable for as long as the element itself is referenced, whatever happened to
	// the document's Lua variable, and the tree is freed when the last of them is collected — including when
	// that is during lua_close, which runs before Impl's members die, so MemCtx is still there to be released
	// into. HtmlDoc allocates outside Lua's allocator, so parse charges the tree against the state's ledger by
	// hand (input size times 4: a lexbor tree costs several times its text) and the deleter above gives it back.
	// Nothing raises a Lua error while a document is owned by a local C++ object: luaL_error longjmps, which
	// runs no destructors, so parse hands the document over to the shared_ptr or deletes it before it errors.

	static const char* const kHtmlDocMt = "bp.html.doc";
	static const char* const kHtmlElMt = "bp.html.element";

	struct HtmlDocUd
	{
		std::shared_ptr<HtmlDoc> doc;
	};

	struct HtmlElUd
	{
		std::shared_ptr<HtmlDoc> doc;	// what keeps the node's tree alive under it
		HtmlNode node;
	};

	static int htmlDocGc(lua_State* L)
	{
		static_cast<HtmlDocUd*>(lua_touserdata(L, 1))->~HtmlDocUd();
		return 0;
	}

	static int htmlElGc(lua_State* L)
	{
		static_cast<HtmlElUd*>(lua_touserdata(L, 1))->~HtmlElUd();
		return 0;
	}

	// wrong types come back from luaL_check* as a Lua error naming what was wanted, never a crash
	static HtmlDocUd* checkHtmlDoc(lua_State* L, int idx)
	{
		return static_cast<HtmlDocUd*>(luaL_checkudata(L, idx, kHtmlDocMt));
	}

	static HtmlElUd* checkHtmlEl(lua_State* L, int idx)
	{
		return static_cast<HtmlElUd*>(luaL_checkudata(L, idx, kHtmlElMt));
	}

	// nil when there is no node, so selectFirst has the found-or-nil shape of the design's table
	static void pushHtmlElement(lua_State* L, const std::shared_ptr<HtmlDoc>& doc, const HtmlNode& node)
	{
		if (!node.valid())
		{
			lua_pushnil(L);
			return;
		}
		void* p = lua_newuserdatauv(L, sizeof(HtmlElUd), 0);
		new (p) HtmlElUd{doc, node};
		luaL_setmetatable(L, kHtmlElMt);
	}

	static void pushHtmlElements(lua_State* L, const std::shared_ptr<HtmlDoc>& doc,
		const std::vector<HtmlNode>& nodes)
	{
		lua_createtable(L, (int)nodes.size(), 0);
		for (size_t i = 0; i < nodes.size(); i++)
		{
			if (!nodes[i].valid()) continue;	// keep the array free of holes, whatever select handed back
			pushHtmlElement(L, doc, nodes[i]);
			lua_rawseti(L, -2, (lua_Integer)i + 1);
		}
	}

	static int htmlDocSelect(lua_State* L)
	{
		HtmlDocUd* d = checkHtmlDoc(L, 1);
		size_t n = 0;
		const char* c = luaL_checklstring(L, 2, &n);
		std::vector<HtmlNode> nodes = d->doc->select(std::string(c, n));	// HtmlDoc caps the list at 20000
		pushHtmlElements(L, d->doc, nodes);
		return 1;
	}

	static int htmlDocSelectFirst(lua_State* L)
	{
		HtmlDocUd* d = checkHtmlDoc(L, 1);
		size_t n = 0;
		const char* c = luaL_checklstring(L, 2, &n);
		pushHtmlElement(L, d->doc, d->doc->selectFirst(std::string(c, n)));
		return 1;
	}

	static int htmlElText(lua_State* L)
	{
		std::string s = checkHtmlEl(L, 1)->node.text();
		lua_pushlstring(L, s.data(), s.size());
		return 1;
	}

	static int htmlElAttr(lua_State* L)
	{
		HtmlElUd* e = checkHtmlEl(L, 1);
		size_t n = 0;
		const char* a = luaL_checklstring(L, 2, &n);
		// HtmlNode::attr reports an absent attribute as "" and cannot tell it from an empty one; 5.2 wants absent
		// to be nil, and an empty attribute is of no use to a source, so both come back nil.
		std::string v = e->node.attr(std::string(a, n));
		if (v.empty()) lua_pushnil(L);
		else lua_pushlstring(L, v.data(), v.size());
		return 1;
	}

	static int htmlElHtml(lua_State* L)
	{
		std::string s = checkHtmlEl(L, 1)->node.html();
		lua_pushlstring(L, s.data(), s.size());
		return 1;
	}

	static int htmlElTag(lua_State* L)
	{
		std::string s = checkHtmlEl(L, 1)->node.tag();
		lua_pushlstring(L, s.data(), s.size());
		return 1;
	}

	static int htmlElSelect(lua_State* L)
	{
		HtmlElUd* e = checkHtmlEl(L, 1);
		size_t n = 0;
		const char* c = luaL_checklstring(L, 2, &n);
		std::vector<HtmlNode> nodes = e->node.select(std::string(c, n));
		pushHtmlElements(L, e->doc, nodes);	// the children share the element's document, and keep it alive
		return 1;
	}

	static int htmlElSelectFirst(lua_State* L)
	{
		HtmlElUd* e = checkHtmlEl(L, 1);
		size_t n = 0;
		const char* c = luaL_checklstring(L, 2, &n);
		pushHtmlElement(L, e->doc, e->node.selectFirst(std::string(c, n)));
		return 1;
	}

	static int bpHtmlParse(lua_State* L)
	{
		MemCtx* m = static_cast<MemCtx*>(lua_touserdata(L, lua_upvalueindex(1)));
		size_t n = 0;
		const char* s = luaL_checklstring(L, 1, &n);
		// the same refusal as bp.http's response and HtmlDoc::parse's own, said before copying the page about
		if (n > kMaxResponse) return luaL_error(L, "bp.html: the page is larger than 8 MB");
		size_t charge = n * 4;
		if (m->used > m->cap || charge > m->cap - m->used)	// the same shape as cappedAlloc's test
			return luaL_error(L, "bp.html: parsing this page would push the source over its memory limit");
		HtmlDoc* doc = new HtmlDoc();
		if (!doc->parse(std::string(s, n)))
		{
			delete doc;	// given back before the longjmp, which would not run any destructor for us
			return luaL_error(L, "bp.html: the page could not be parsed");
		}
		m->used += charge;
		std::shared_ptr<HtmlDoc> owned(doc, [m, charge](HtmlDoc* d) {
			delete d;
			m->used -= charge;
		});
		void* p = lua_newuserdatauv(L, sizeof(HtmlDocUd), 0);
		new (p) HtmlDocUd{std::move(owned)};
		luaL_setmetatable(L, kHtmlDocMt);
		return 1;
	}

	// one metatable: __gc frees the struct's C++ members, __index is the method table (setfuncs fills the table on
	// top of the stack, so the methods get their own table rather than living on the metatable itself)
	static void htmlMeta(lua_State* L, const luaL_Reg* methods, lua_CFunction gc, const char* name)
	{
		if (!luaL_newmetatable(L, name))
		{
			lua_pop(L, 1);
			return;
		}
		lua_pushcfunction(L, gc);
		lua_setfield(L, -2, "__gc");
		lua_newtable(L);
		luaL_setfuncs(L, methods, 0);
		lua_setfield(L, -2, "__index");
		lua_pop(L, 1);
	}

	static void htmlInstall(lua_State* L)
	{
		static const luaL_Reg docMethods[] = {
			{"select", htmlDocSelect},
			{"selectFirst", htmlDocSelectFirst},
			{nullptr, nullptr}
		};
		static const luaL_Reg elMethods[] = {
			{"text", htmlElText},
			{"attr", htmlElAttr},
			{"html", htmlElHtml},
			{"tag", htmlElTag},
			{"select", htmlElSelect},
			{"selectFirst", htmlElSelectFirst},
			{nullptr, nullptr}
		};
		htmlMeta(L, docMethods, htmlDocGc, kHtmlDocMt);
		htmlMeta(L, elMethods, htmlElGc, kHtmlElMt);
	}

	// The stock wrappers, with our hook attached to the new thread: a count hook is per thread and would otherwise
	// let a coroutine loop forever.
	static int auxResume(lua_State* L, lua_State* co, int narg)
	{
		int status, nres;
		if (!lua_checkstack(co, narg))
		{
			lua_pushliteral(L, "too many arguments to resume");
			return -1;
		}
		lua_xmove(L, co, narg);
		status = lua_resume(co, L, narg, &nres);
		if (status == LUA_OK || status == LUA_YIELD)
		{
			if (!lua_checkstack(L, nres + 1))
			{
				lua_pop(co, nres);
				lua_pushliteral(L, "too many results to resume");
				return -1;
			}
			lua_xmove(co, L, nres);
			return nres;
		}
		lua_xmove(co, L, 1);
		return -1;
	}

	static int coAuxWrap(lua_State* L)
	{
		lua_State* co = lua_tothread(L, lua_upvalueindex(1));
		int r = auxResume(L, co, lua_gettop(L));
		if (r < 0)
		{
			int stat = lua_status(co);
			if (stat != LUA_OK && stat != LUA_YIELD)
			{
				stat = lua_closethread(co, L);
				lua_xmove(co, L, 1);
			}
			if (stat != LUA_ERRMEM && lua_type(L, -1) == LUA_TSTRING)
			{
				luaL_where(L, 1);
				lua_insert(L, -2);
				lua_concat(L, 2);
			}
			return lua_error(L);
		}
		return r;
	}

	static int coCreate(lua_State* L)
	{
		luaL_checktype(L, 1, LUA_TFUNCTION);
		lua_State* NL = lua_newthread(L);
		lua_sethook(NL, budgetHook, LUA_MASKCOUNT, kHookInstructions);
		lua_pushvalue(L, 1);
		lua_xmove(L, NL, 1);
		return 1;
	}

	static int coWrap(lua_State* L)
	{
		coCreate(L);
		lua_pushcclosure(L, coAuxWrap, 1);
		return 1;
	}

	// ---- the host ----

	SourceHost::SourceHost() : impl_(new Impl) {}
	SourceHost::~SourceHost() = default;

	void SourceHost::setHttp(HttpFn fn)
	{
		impl_->http = std::move(fn);
	}

	void SourceHost::setPrefsPath(std::string path)
	{
		impl_->prefsPath = std::move(path);
		impl_->prefsLoaded = false;
		impl_->loadPrefs();
	}

	void SourceHost::setLimits(double wallSeconds, uint64_t instructions, size_t memoryBytes)
	{
		if (wallSeconds > 0) impl_->wallSeconds = wallSeconds;
		if (instructions > 0) impl_->instructionBudget = instructions;
		if (memoryBytes > 0)
		{
			impl_->memoryCap = memoryBytes;
			impl_->mem.cap = memoryBytes;
		}
	}

	bool SourceHost::load(const std::string& scriptText, const std::string& expectedId, std::string& error)
	{
		error.clear();
		impl_->dirty = false;
		impl_->closeState();
		impl_->meta = SourceMeta{};
		if (scriptText.size() > 256 * 1024)
		{
			error = "the source script is larger than 256 KB";
			return false;
		}
		impl_->script = scriptText;
		impl_->expectedId = expectedId;
		if (!impl_->prefsLoaded) impl_->loadPrefs();
		if (!impl_->createState(error)) return false;
		if (!impl_->installSource(error))
		{
			impl_->closeState();
			impl_->meta = SourceMeta{};
			return false;
		}
		return true;
	}

	bool SourceHost::popular(int page, MangaList& out, std::string& error)
	{
		error.clear();
		out = MangaList{};
		if (!impl_->prepare(error)) return false;
		if (!impl_->pushFunction("popular", true, error)) return false;
		lua_pushinteger(impl_->L, page);
		if (!impl_->callFunction("popular", 1, error)) return false;
		bool ok = impl_->readList(impl_->L, -1, out, error);
		lua_pop(impl_->L, 1);
		return ok;
	}

	bool SourceHost::latest(int page, MangaList& out, std::string& error)
	{
		error.clear();
		out = MangaList{};
		if (!impl_->prepare(error)) return false;
		if (!impl_->pushFunction("latest", false, error))
		{
			error = "the source has no latest function";
			return false;
		}
		lua_pushinteger(impl_->L, page);
		if (!impl_->callFunction("latest", 1, error)) return false;
		bool ok = impl_->readList(impl_->L, -1, out, error);
		lua_pop(impl_->L, 1);
		return ok;
	}

	bool SourceHost::search(const std::string& query, int page, const std::string& filtersJson,
		MangaList& out, std::string& error)
	{
		error.clear();
		out = MangaList{};
		if (!impl_->prepare(error)) return false;
		int base = lua_gettop(impl_->L);
		if (!impl_->pushFunction("search", false, error))
		{
			error = "the source has no search function";
			return false;
		}
		lua_pushlstring(impl_->L, query.data(), query.size());
		lua_pushinteger(impl_->L, page);
		if (filtersJson.empty())
		{
			lua_newtable(impl_->L);
		}
		else
		{
			JVal v;
			std::string jerr;
			if (!parseJson(filtersJson, v, jerr))
			{
				lua_settop(impl_->L, base);
				error = "the filter values aren't JSON: " + jerr;
				return false;
			}
			if (!pushJson(impl_->L, v, 0))
			{
				lua_settop(impl_->L, base);
				error = "the filter values are nested too deeply";
				return false;
			}
		}
		if (!impl_->callFunction("search", 3, error)) return false;
		bool ok = impl_->readList(impl_->L, -1, out, error);
		lua_pop(impl_->L, 1);
		return ok;
	}

	bool SourceHost::filters(std::string& json, std::string& error)
	{
		error.clear();
		json = "[]";
		if (!impl_->prepare(error)) return false;
		if (!impl_->pushFunction("filters", false, error)) return true;
		if (!impl_->callFunction("filters", 0, error)) return false;
		if (lua_isnil(impl_->L, -1))
		{
			lua_pop(impl_->L, 1);
			return true;
		}
		JVal v;
		std::string jerr;
		std::set<const void*> seen;
		if (!luaToJson(impl_->L, -1, v, jerr, 0, seen))
		{
			lua_pop(impl_->L, 1);
			error = "filters(): " + jerr;
			return false;
		}
		lua_pop(impl_->L, 1);
		if (v.type == JVal::Obj && v.o.empty()) v.type = JVal::Arr;	// an empty {} is an empty list here
		JVal norm;
		FilterBudget budget;
		if (!normalizeFilterList(v, norm, budget, error)) return false;
		json.clear();
		encodeJson(norm, json);
		return true;
	}

	bool SourceHost::details(const std::string& seriesJson, MangaDetails& out, std::string& error)
	{
		error.clear();
		out = MangaDetails{};
		if (!impl_->prepare(error)) return false;
		int base = lua_gettop(impl_->L);
		if (!impl_->pushFunction("details", true, error)) return false;
		if (seriesJson.empty())
		{
			lua_newtable(impl_->L);
		}
		else
		{
			JVal v;
			std::string jerr;
			if (!parseJson(seriesJson, v, jerr))
			{
				lua_settop(impl_->L, base);
				error = "the series isn't JSON: " + jerr;
				return false;
			}
			if (!pushJson(impl_->L, v, 0))
			{
				lua_settop(impl_->L, base);
				error = "the series is nested too deeply";
				return false;
			}
		}
		if (!impl_->callFunction("details", 1, error)) return false;
		bool ok = impl_->readDetails(impl_->L, -1, out, error);
		lua_pop(impl_->L, 1);
		return ok;
	}

	bool SourceHost::chapters(const std::string& seriesJson, std::vector<MangaChapter>& out,
		std::string& error)
	{
		error.clear();
		out.clear();
		if (!impl_->prepare(error)) return false;
		int base = lua_gettop(impl_->L);
		if (!impl_->pushFunction("chapters", true, error)) return false;
		if (seriesJson.empty())
		{
			lua_newtable(impl_->L);
		}
		else
		{
			JVal v;
			std::string jerr;
			if (!parseJson(seriesJson, v, jerr))
			{
				lua_settop(impl_->L, base);
				error = "the series isn't JSON: " + jerr;
				return false;
			}
			if (!pushJson(impl_->L, v, 0))
			{
				lua_settop(impl_->L, base);
				error = "the series is nested too deeply";
				return false;
			}
		}
		if (!impl_->callFunction("chapters", 1, error)) return false;
		bool ok = impl_->readChapters(impl_->L, -1, out, error);
		lua_pop(impl_->L, 1);
		return ok;
	}

	bool SourceHost::pages(const std::string& chapterJson, std::vector<MangaPage>& out, std::string& error)
	{
		error.clear();
		out.clear();
		if (!impl_->prepare(error)) return false;
		int base = lua_gettop(impl_->L);
		if (!impl_->pushFunction("pages", true, error)) return false;
		if (chapterJson.empty())
		{
			lua_newtable(impl_->L);
		}
		else
		{
			JVal v;
			std::string jerr;
			if (!parseJson(chapterJson, v, jerr))
			{
				lua_settop(impl_->L, base);
				error = "the chapter isn't JSON: " + jerr;
				return false;
			}
			if (!pushJson(impl_->L, v, 0))
			{
				lua_settop(impl_->L, base);
				error = "the chapter is nested too deeply";
				return false;
			}
		}
		if (!impl_->callFunction("pages", 1, error)) return false;
		bool ok = impl_->readPages(impl_->L, -1, out, error);
		lua_pop(impl_->L, 1);
		return ok;
	}

	bool SourceHost::image(const std::string& pageJson, MangaImage& out, std::string& error)
	{
		error.clear();
		out = MangaImage{};
		if (!impl_->prepare(error)) return false;
		int base = lua_gettop(impl_->L);
		if (!impl_->pushFunction("image", false, error))
		{
			error = "the source has no image function";
			return false;
		}
		if (pageJson.empty())
		{
			lua_newtable(impl_->L);
		}
		else
		{
			JVal v;
			std::string jerr;
			if (!parseJson(pageJson, v, jerr))
			{
				lua_settop(impl_->L, base);
				error = "the page isn't JSON: " + jerr;
				return false;
			}
			if (!pushJson(impl_->L, v, 0))
			{
				lua_settop(impl_->L, base);
				error = "the page is nested too deeply";
				return false;
			}
		}
		if (!impl_->callFunction("image", 1, error)) return false;
		bool ok = impl_->readImage(impl_->L, -1, out, error);
		lua_pop(impl_->L, 1);
		return ok;
	}

	const SourceMeta& SourceHost::meta() const
	{
		return impl_->meta;
	}

	bool SourceHost::loaded() const
	{
		return impl_->L != nullptr;
	}
}
