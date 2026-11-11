// Manga source repository indexes (design 5.3), pure. src/json.h is a writer only, so the reading lives here:
// small and tolerant in shape (trailing commas) but nothing more — a misread index would install the wrong files,
// so anything that isn't clearly JSON is refused, and an index is as untrusted as the scripts it lists.
#include "mangarepo.h"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <openssl/evp.h>

namespace bp
{
	namespace
	{
		// ---- the tolerant JSON reader ----

		struct JValue
		{
			enum class Kind { Null, Bool, Num, Str, Arr, Obj };
			Kind kind = Kind::Null;
			bool boolean = false;
			std::string text;	// strings unescaped, numbers as written
			std::vector<JValue> items;
			std::vector<std::pair<std::string, JValue>> members;
			const JValue* find(const std::string& key) const
			{
				for (const auto& kv : members)
					if (kv.first == key) return &kv.second;
				return nullptr;
			}
		};

		// Recursive descent with a depth cap: "[[[[[…" from a hostile index must be refused, not overflow the
		// stack (design 5.4's distrust applies to the index, not only to the scripts). Trailing commas are
		// tolerated — hand-maintained indexes drift — but nothing that could change a value's shape.
		struct JReader
		{
			const std::string& s;
			size_t i = 0;

			void ws()
			{
				while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
			}
			bool lit(const char* w)
			{
				size_t n = strlen(w);
				if (s.compare(i, n, w) != 0) return false;
				i += n;
				return true;
			}
			void utf8(std::string& out, unsigned cp)	// an unpaired surrogate arrives as U+FFFD
			{
				if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
				if (cp < 0x80) out += (char)cp;
				else if (cp < 0x800) { out += (char)(0xC0 | cp >> 6); out += (char)(0x80 | (cp & 0x3F)); }
				else if (cp < 0x10000) { out += (char)(0xE0 | cp >> 12); out += (char)(0x80 | (cp >> 6 & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
				else { out += (char)(0xF0 | cp >> 18); out += (char)(0x80 | (cp >> 12 & 0x3F)); out += (char)(0x80 | (cp >> 6 & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
			}
			int hex4(unsigned& cp)
			{
				cp = 0;
				for (int k = 0; k < 4; k++)
				{
					if (i >= s.size()) return 0;
					char c = s[i++];
					if (c >= '0' && c <= '9') cp = cp * 16 + (c - '0');
					else if (c >= 'a' && c <= 'f') cp = cp * 16 + (c - 'a' + 10);
					else if (c >= 'A' && c <= 'F') cp = cp * 16 + (c - 'A' + 10);
					else return 0;
				}
				return 1;
			}
			bool str(std::string& out)
			{
				i++;	// the opening quote
				while (i < s.size())
				{
					char c = s[i];
					if (c == '"') { i++; return true; }
					if ((unsigned char)c < 0x20) return false;	// a raw control character: the escape was meant
					if (c != '\\') { out += c; i++; continue; }
					i++;
					if (i >= s.size()) return false;
					char e = s[i++];
					if (e == 'u')
					{
						unsigned cp = 0;
						if (!hex4(cp)) return false;
						if (cp >= 0xD800 && cp <= 0xDBFF && s.compare(i, 2, "\\u") == 0)
						{
							i += 2;
							unsigned lo = 0;
							if (!hex4(lo)) return false;
							if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
							else { utf8(out, cp); cp = lo; }	// not a pair: keep both, broken
						}
						utf8(out, cp);
					}
					else if (e == '"') out += '"';
					else if (e == '\\') out += '\\';
					else if (e == '/') out += '/';
					else if (e == 'b') out += '\b';
					else if (e == 'f') out += '\f';
					else if (e == 'n') out += '\n';
					else if (e == 'r') out += '\r';
					else if (e == 't') out += '\t';
					else return false;
				}
				return false;	// ran out inside the string
			}
			bool value(JValue& v, int depth)
			{
				if (depth > 64) return false;
				ws();
				if (i >= s.size()) return false;
				char c = s[i];
				if (c == '{')
				{
					v.kind = JValue::Kind::Obj;
					i++;
					ws();
					if (i < s.size() && s[i] == '}') { i++; return true; }
					for (;;)
					{
						ws();
						std::string key;
						if (i >= s.size() || s[i] != '"' || !str(key)) return false;
						ws();
						if (i >= s.size() || s[i] != ':') return false;
						i++;
						v.members.emplace_back(key, JValue());
						if (!value(v.members.back().second, depth + 1)) return false;
						ws();
						if (i >= s.size()) return false;
						if (s[i] == ',') { i++; continue; }
						if (s[i] == '}') { i++; return true; }
						return false;
					}
				}
				if (c == '[')
				{
					v.kind = JValue::Kind::Arr;
					i++;
					ws();
					if (i < s.size() && s[i] == ']') { i++; return true; }
					for (;;)
					{
						v.items.emplace_back();
						if (!value(v.items.back(), depth + 1)) return false;
						ws();
						if (i >= s.size()) return false;
						if (s[i] == ',') { i++; continue; }
						if (s[i] == ']') { i++; return true; }
						return false;
					}
				}
				if (c == '"')
				{
					v.kind = JValue::Kind::Str;
					return str(v.text);
				}
				if (lit("true")) { v.kind = JValue::Kind::Bool; v.boolean = true; return true; }
				if (lit("false")) { v.kind = JValue::Kind::Bool; v.boolean = false; return true; }
				if (lit("null")) { v.kind = JValue::Kind::Null; return true; }
				size_t start = i;
				if (s[i] == '-') i++;
				while (i < s.size() && isdigit((unsigned char)s[i])) i++;
				if (i < s.size() && s[i] == '.')
				{
					i++;
					while (i < s.size() && isdigit((unsigned char)s[i])) i++;
				}
				if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
				{
					i++;
					if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
					while (i < s.size() && isdigit((unsigned char)s[i])) i++;
				}
				// a bare word ("yes") is not a number: at least one digit must have been read
				size_t digits = 0;
				for (size_t k = start; k < i; k++) if (isdigit((unsigned char)s[k])) digits++;
				if (!digits) return false;
				v.kind = JValue::Kind::Num;
				v.text = s.substr(start, i - start);
				return true;
			}
		};

		bool readJson(const std::string& s, JValue& root)
		{
			JReader r{s};
			if (!r.value(root, 0)) return false;
			r.ws();
			return r.i == s.size();	// trailing junk means it wasn't one index
		}

		// ---- validation ----

		bool ciPrefix(const std::string& s, const char* p)
		{
			size_t n = strlen(p);
			if (s.size() < n) return false;
			for (size_t i = 0; i < n; i++)
				if (tolower((unsigned char)s[i]) != tolower((unsigned char)p[i])) return false;
			return true;
		}

		// "scheme:" before the first "/", per RFC 3986 (a relative path can't hold a colon before its first slash)
		bool hasScheme(const std::string& u)
		{
			size_t colon = u.find(':');
			if (colon == std::string::npos || colon == 0) return false;
			if (!isalpha((unsigned char)u[0])) return false;
			for (size_t i = 1; i < colon; i++)
				if (!(isalnum((unsigned char)u[i]) || u[i] == '+' || u[i] == '-' || u[i] == '.')) return false;
			return true;
		}

		bool noBadChars(const std::string& u)	// a space or control character is never part of a URL here
		{
			for (char c : u)
				if ((unsigned char)c <= 0x20 || (unsigned char)c == 0x7F) return false;
			return true;
		}

		bool validId(const std::string& id)	// [a-z0-9._-]{1,64}, and not all dots: "<id>.lua" is a filename
		{
			if (id.empty() || id.size() > 64) return false;
			bool any = false;
			for (char c : id)
			{
				if (!(c >= 'a' && c <= 'z' || c >= '0' && c <= '9' || c == '.' || c == '_' || c == '-')) return false;
				if (c != '.') any = true;
			}
			return any;
		}

		bool validVersion(const std::string& v)	// dotted numbers: "1", "1.2", "1.2.0"
		{
			if (v.empty() || v.size() > 64) return false;
			bool digitInRun = false;
			for (char c : v)
			{
				if (c == '.') { if (!digitInRun) return false; digitInRun = false; }
				else if (isdigit((unsigned char)c)) digitInRun = true;
				else return false;
			}
			return digitInRun;
		}

		bool validSha(const std::string& h)
		{
			if (h.size() != 64) return false;
			for (char c : h)
				if (!isxdigit((unsigned char)c)) return false;
			return true;
		}

		bool validLang(const std::string& l)	// a short code, its dash suffix at most, or "all"
		{
			if (l.empty() || l.size() > 35) return false;
			if (l.size() == 3 && ciPrefix(l, "all")) return true;
			size_t i = 0;
			while (i < l.size() && isalpha((unsigned char)l[i]) && i < 3) i++;
			if (i < 2) return false;
			if (i == l.size()) return true;
			if (l[i] != '-') return false;
			i++;
			size_t j = i;
			while (j < l.size() && (isalnum((unsigned char)l[j]))) j++;
			return j == l.size() && j - i >= 2 && j - i <= 8;
		}

		// a source's file or icon: relative (design 5.3 resolves these against the index), or an absolute
		// https:// URL — the design's repositories are HTTPS, and no rule lets a plain http one through
		bool validFileRef(const std::string& u, std::string& why)
		{
			if (u.empty()) { why = "empty"; return false; }
			if (!noBadChars(u)) { why = "holds a space or control character"; return false; }
			if (hasScheme(u))
			{
				if (!ciPrefix(u, "https://")) { why = "absolute but not https://"; return false; }
				if (!validRepoUrl(u)) { why = "is not a well-formed URL"; return false; }
			}
			return true;
		}

		// RFC 3986 6.2.2, the parts that matter here: drop "." and empty segments, fold ".." back but never
		// past the root, and keep a trailing slash when the path or its last real segment ("." / "..") ended on one
		std::string foldPath(const std::string& in)
		{
			bool absolute = !in.empty() && in[0] == '/';
			std::vector<std::string> segs;
			std::string seg, last;
			size_t i = 0;
			for (;;)
			{
				if (i == in.size() || in[i] == '/')
				{
					if (!seg.empty())
					{
						last = seg;
						if (seg == "..") { if (!segs.empty()) segs.pop_back(); }
						else if (seg != ".") segs.push_back(seg);
					}
					seg.clear();
					if (i == in.size()) break;
					i++;
					continue;
				}
				seg += in[i++];
			}
			bool trail = !in.empty() && (in.back() == '/' || last == "." || last == "..");
			std::string out = absolute ? "/" : "";
			for (size_t k = 0; k < segs.size(); k++)
			{
				if (k) out += '/';
				out += segs[k];
			}
			if (trail && !segs.empty()) out += '/';
			return out;
		}
	}

	bool parseRepoIndex(const std::string& json, RepoIndex& out, std::string& error)
	{
		out = RepoIndex{};
		error.clear();
		if (json.size() > 1024 * 1024)
		{
			error = "index larger than 1 MB";
			return false;
		}
		JValue root;
		if (!readJson(json, root) || root.kind != JValue::Kind::Obj)
		{
			error = "not a repository index (JSON did not read)";
			return false;
		}
		// format: newer than this build reads is refused, as a script with a newer `api` is (design 5.2)
		if (const JValue* f = root.find("format"))
		{
			if (f->kind != JValue::Kind::Num && f->kind != JValue::Kind::Str)
			{
				error = "format is not a number";
				return false;
			}
			double fmt = strtod(f->text.c_str(), nullptr);
			if (fmt < 1)
			{
				error = "format must be 1 or newer";
				return false;
			}
			if (fmt > 1)
			{
				error = "index format " + f->text + ": needs a newer Banana Player";
				return false;
			}
		}
		if (const JValue* n = root.find("name"); n && n->kind == JValue::Kind::Str) out.name = n->text;
		if (const JValue* h = root.find("homepage"); h && h->kind == JValue::Kind::Str) out.homepage = h->text;
		const JValue* sources = root.find("sources");
		if (!sources || sources->kind != JValue::Kind::Arr)
		{
			error = "no sources list";
			return false;
		}
		if (sources->items.size() > 2000)	// an index that big is a flood, not a repository
		{
			error = "more than 2000 sources";
			return false;
		}
		std::string skipped;
		for (size_t k = 0; k < sources->items.size(); k++)
		{
			const JValue& e = sources->items[k];
			RepoSource rs;
			std::string why;
			const JValue* v;
			if (e.kind != JValue::Kind::Obj) why = "entry is not an object";
			if (why.empty() && (!(v = e.find("id")) || v->kind != JValue::Kind::Str)) why = "no id";
			if (why.empty()) { rs.id = v->text; if (!validId(rs.id)) why = "bad id"; }
			if (why.empty() && (!(v = e.find("version")) || v->kind != JValue::Kind::Str)) why = "no version";
			if (why.empty()) { rs.version = v->text; if (!validVersion(rs.version)) why = "bad version"; }
			if (why.empty() && (!(v = e.find("sha256")) || v->kind != JValue::Kind::Str)) why = "no sha256";
			if (why.empty()) { rs.sha256 = v->text; if (!validSha(rs.sha256)) why = "bad sha256"; }
			if (why.empty() && (!(v = e.find("file")) || v->kind != JValue::Kind::Str)) why = "no file";
			if (why.empty()) { rs.url = v->text; if (!validFileRef(rs.url, why)) why = "file " + why; }
			if (why.empty() && (!(v = e.find("lang")) || v->kind != JValue::Kind::Str)) why = "no lang";
			if (why.empty()) { rs.lang = v->text; if (!validLang(rs.lang)) why = "bad lang"; }
			if (why.empty() && (!(v = e.find("name")) || v->kind != JValue::Kind::Str || v->text.empty())) why = "no name";
			if (why.empty()) rs.name = v->text;
			if (why.empty())
			{
				if (const JValue* i = e.find("icon"); i && i->kind == JValue::Kind::Str && !i->text.empty())
				{
					rs.icon = i->text;
					if (!validFileRef(rs.icon, why)) why = "icon " + why;
				}
			}
			if (why.empty())
			{
				// api (the contract version this source needs) and nsfw are optional; read as text and bool
				if (const JValue* i = e.find("api"); i && (i->kind == JValue::Kind::Num || i->kind == JValue::Kind::Str))
					rs.minApp = i->text;
				if (const JValue* i = e.find("nsfw"); i && i->kind == JValue::Kind::Bool) rs.adult = i->boolean;
			}
			if (why.empty()) out.sources.push_back(std::move(rs));
			else	// named in error, the rest kept: one repo author's typo is not every source's fault
			{
				if (!skipped.empty()) skipped += "; ";
				skipped += "skipped source " + std::to_string(k + 1)
					+ (e.kind == JValue::Kind::Obj && e.find("id") && e.find("id")->kind == JValue::Kind::Str
						? " (" + e.find("id")->text + ")" : "") + ": " + why;
			}
		}
		error = skipped;
		return true;
	}

	std::string repoResolve(const std::string& indexUrl, const std::string& href)
	{
		if (href.empty()) return indexUrl;
		if (hasScheme(href)) return href;	// already absolute; parseRepoIndex weighed its scheme
		if (href.rfind("//", 0) == 0)	// protocol-relative: the index's scheme, that host
			return indexUrl.substr(0, indexUrl.find("://") + 3) + foldPath(href.substr(2));
		size_t p = indexUrl.find("://");
		if (p == std::string::npos) return href;	// nothing to resolve against; leave it as it came
		std::string head = indexUrl.substr(0, p + 3);
		std::string rest = indexUrl.substr(p + 3);
		size_t a = rest.find_first_of("/?#");
		std::string authority = a == std::string::npos ? rest : rest.substr(0, a);
		std::string base = a == std::string::npos ? std::string() : rest.substr(a);
		size_t q = base.find_first_of("?#");	// the index's own query is not part of its folder
		if (q != std::string::npos) base = base.substr(0, q);
		// the href's query and fragment belong to the file, not to the path fold
		std::string path = href, tail;
		size_t h = path.find_first_of("?#");
		if (h != std::string::npos) { tail = path.substr(h); path = path.substr(0, h); }
		if (!path.empty() && path[0] == '/')	// from the host's root
			return head + authority + foldPath(path) + tail;
		size_t s = base.rfind('/');	// from the index's folder
		std::string dir = s == std::string::npos ? std::string() : base.substr(0, s + 1);
		std::string folded = foldPath(dir + path);
		if (folded.empty() || folded[0] != '/') folded = "/" + folded;	// base had no path at all ("https://host")
		return head + authority + folded + tail;
	}

	std::string sha256Hex(const uint8_t* data, size_t n)
	{
		if (n && !data) return "";
		unsigned char md[EVP_MAX_MD_SIZE];
		unsigned len = 0;
		// one-shot EVP_Digest, as webauth.cpp hashes its tokens; an index buffer is already whole in memory
		if (EVP_Digest(data ? data : (const unsigned char*)"", n, md, &len, EVP_sha256(), nullptr) != 1) return "";
		static const char* H = "0123456789abcdef";
		std::string out;
		out.reserve(len * 2);
		for (unsigned i = 0; i < len; i++) { out += H[md[i] >> 4]; out += H[md[i] & 15]; }
		return out;
	}

	bool sha256Matches(const std::string& expectedHex, const uint8_t* data, size_t n)
	{
		std::string got = sha256Hex(data, n);
		if (got.empty() || expectedHex.size() != got.size()) return false;
		for (size_t i = 0; i < got.size(); i++)
			if (tolower((unsigned char)expectedHex[i]) != got[i]) return false;
		return true;
	}

	bool updateAvailable(const std::string& installed, const std::string& indexed)
	{
		return compareVersions(indexed, installed) > 0;
	}

	bool validRepoUrl(const std::string& url)
	{
		if (url.empty() || url.size() > 2048) return false;
		if (!noBadChars(url)) return false;
		// http(s) only, any case ("HTTPS://" is legal if odd); the design's https rule sits on top in parseRepoIndex
		if (!ciPrefix(url, "http://") && !ciPrefix(url, "https://")) return false;
		std::string rest = url.substr(url.find("://") + 3);
		size_t a = rest.find_first_of("/?#");
		std::string authority = a == std::string::npos ? rest : rest.substr(0, a);
		if (authority.empty()) return false;	// "https://" alone names no host
		if (authority.find('@') != std::string::npos) return false;	// credentials, never (CLAUDE.md)
		return true;
	}
}
