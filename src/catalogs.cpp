// Books › Catalogs, the parts without a window (catalogs.h).
#include "catalogs.h"
#include "filesave.h"
#include "secret.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <openssl/evp.h>
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		std::string lower(std::string s)
		{
			for (char& c : s) c = (char)std::tolower((unsigned char)c);
			return s;
		}

		std::string trim(const std::string& s)
		{
			size_t a = 0, b = s.size();
			while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
			while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
			return s.substr(a, b - a);
		}

		std::string md5Hex(const std::string& s)
		{
			unsigned char out[EVP_MAX_MD_SIZE];
			unsigned int n = 0;
			EVP_Digest(s.data(), s.size(), out, &n, EVP_md5(), nullptr);
			static const char* hex = "0123456789abcdef";
			std::string h;
			for (unsigned i = 0; i < n; i++)
			{
				h += hex[out[i] >> 4];
				h += hex[out[i] & 15];
			}
			return h;
		}

		// a challenge's parameters: name=value or name="quoted, with \" escapes", comma-separated
		void readParams(const std::string& s, size_t at, HttpChallenge& c)
		{
			while (at < s.size())
			{
				while (at < s.size() && (s[at] == ' ' || s[at] == ',' || s[at] == '\t')) at++;
				size_t eq = s.find('=', at);
				if (eq == std::string::npos) return;
				std::string name = lower(trim(s.substr(at, eq - at)));
				at = eq + 1;
				while (at < s.size() && s[at] == ' ') at++;
				std::string value;
				if (at < s.size() && s[at] == '"')
				{
					at++;
					while (at < s.size() && s[at] != '"')
					{
						if (s[at] == '\\' && at + 1 < s.size()) at++;
						value += s[at++];
					}
					at++;	// the closing quote
				}
				else
				{
					size_t end = s.find(',', at);
					if (end == std::string::npos) end = s.size();
					value = trim(s.substr(at, end - at));
					at = end;
				}
				if (name == "realm") c.realm = value;
				else if (name == "nonce") c.nonce = value;
				else if (name == "opaque") c.opaque = value;
				else if (name == "qop") c.qop = value;
				else if (name == "algorithm") c.algorithm = value;
				else if (name == "stale") c.stale = lower(value) == "true";
			}
		}

		// the qop list offered ("auth,auth-int"): "auth" when it is there, "" when none was offered (RFC 2069)
		std::string chosenQop(const std::string& offered)
		{
			if (offered.empty()) return "";
			size_t at = 0;
			while (at <= offered.size())
			{
				size_t end = offered.find(',', at);
				if (end == std::string::npos) end = offered.size();
				if (lower(trim(offered.substr(at, end - at))) == "auth") return "auth";
				at = end + 1;
			}
			return "-";	// only auth-int: not one we answer
		}

		std::string pct(const std::string& s)
		{
			static const char* const hex = "0123456789ABCDEF";
			std::string o;
			for (unsigned char c : s)
			{
				if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') o += (char)c;
				else
				{
					o += '%';
					o += hex[c >> 4];
					o += hex[c & 15];
				}
			}
			return o;
		}

		void replaceAll(std::string& s, const std::string& from, const std::string& to)
		{
			size_t p = 0;
			while ((p = s.find(from, p)) != std::string::npos)
			{
				s.replace(p, from.size(), to);
				p += to.size();
			}
		}

		bool fileThere(const std::string& path)
		{
			struct stat st;
			return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
		}
	}

	// ---- the sign-in ----

	bool parseAuthChallenge(const std::vector<std::string>& values, HttpChallenge& out)
	{
		HttpChallenge basic;
		bool haveBasic = false;
		for (const std::string& v : values)
		{
			std::string t = trim(v);
			size_t sp = t.find(' ');
			std::string scheme = lower(t.substr(0, sp));
			if (scheme == "digest")
			{
				HttpChallenge c;
				c.scheme = "digest";
				readParams(t, sp == std::string::npos ? t.size() : sp + 1, c);
				std::string alg = lower(c.algorithm);
				if (c.nonce.empty() || !(alg.empty() || alg == "md5" || alg == "md5-sess")) continue;
				if (chosenQop(c.qop) == "-") continue;
				out = c;
				return true;	// the stronger one: taken as soon as it is seen
			}
			if (scheme == "basic" && !haveBasic)
			{
				basic.scheme = "basic";
				readParams(t, sp == std::string::npos ? t.size() : sp + 1, basic);
				haveBasic = true;
			}
		}
		if (haveBasic) out = basic;
		return haveBasic;
	}

	std::string basicAuthorization(const std::string& user, const std::string& password)
	{
		return "Basic " + base64Encode(user + ":" + password);
	}

	std::string digestAuthorization(const HttpChallenge& c, const std::string& user, const std::string& password,
		const std::string& method, const std::string& uri, const std::string& cnonce, unsigned nc)
	{
		char ncText[9];
		snprintf(ncText, sizeof ncText, "%08x", nc);
		std::string qop = chosenQop(c.qop);
		std::string ha1 = md5Hex(user + ":" + c.realm + ":" + password);
		if (lower(c.algorithm) == "md5-sess") ha1 = md5Hex(ha1 + ":" + c.nonce + ":" + cnonce);
		std::string ha2 = md5Hex(method + ":" + uri);
		std::string response = qop.empty() ? md5Hex(ha1 + ":" + c.nonce + ":" + ha2)
			: md5Hex(ha1 + ":" + c.nonce + ":" + ncText + ":" + cnonce + ":" + qop + ":" + ha2);
		auto quoted = [](const std::string& s) {
			std::string o = "\"";
			for (char ch : s)
			{
				if (ch == '"' || ch == '\\') o += '\\';
				o += ch;
			}
			return o + "\"";
		};
		std::string h = "Digest username=" + quoted(user) + ", realm=" + quoted(c.realm) + ", nonce=" + quoted(c.nonce) +
			", uri=" + quoted(uri);
		if (!c.algorithm.empty()) h += ", algorithm=" + c.algorithm;
		h += ", response=" + quoted(response);
		if (!c.opaque.empty()) h += ", opaque=" + quoted(c.opaque);
		if (!qop.empty()) h += ", qop=" + qop + ", nc=" + ncText + ", cnonce=" + quoted(cnonce);
		return h;
	}

	std::string urlOrigin(const std::string& url)
	{
		size_t colon = url.find("://");
		if (colon == std::string::npos) return "";
		std::string scheme = lower(url.substr(0, colon));
		if (scheme != "http" && scheme != "https") return "";
		size_t start = colon + 3, end = url.find_first_of("/?#", start);
		std::string auth = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
		size_t at = auth.rfind('@');
		if (at != std::string::npos) auth = auth.substr(at + 1);	// a user:password@ part names no other host
		std::string host = auth, port;
		size_t close = auth.find(']');
		size_t pc = auth.rfind(':');
		if (pc != std::string::npos && (close == std::string::npos || pc > close))
		{
			host = auth.substr(0, pc);
			port = auth.substr(pc + 1);
		}
		if (host.empty()) return "";
		if (port.empty()) port = scheme == "https" ? "443" : "80";
		return scheme + "://" + lower(host) + ":" + port;
	}

	bool sameOrigin(const std::string& a, const std::string& b)
	{
		std::string oa = urlOrigin(a);
		return !oa.empty() && oa == urlOrigin(b);
	}

	std::string urlRequestTarget(const std::string& url)
	{
		size_t colon = url.find("://");
		size_t start = colon == std::string::npos ? 0 : colon + 3;
		size_t path = url.find_first_of("/?#", start);
		if (path == std::string::npos) return "/";
		std::string t = url.substr(path);
		size_t hash = t.find('#');
		if (hash != std::string::npos) t.resize(hash);
		if (t.empty() || t[0] == '?') t = "/" + t;
		return t;
	}

	std::string fillSearchTerms(const std::string& templ, const std::string& words)
	{
		std::string s = templ, q = pct(words);
		replaceAll(s, "{searchTerms}", q);
		replaceAll(s, "%7BsearchTerms%7D", q);
		replaceAll(s, "%7bsearchTerms%7d", q);
		return s;
	}

	// ---- the file a book is saved as ----

	std::string exfatName(const std::string& s, size_t maxBytes)
	{
		std::string o;
		for (unsigned char c : s)
		{
			if (c < 0x20 || c == 0x7F) c = ' ';
			else if (std::strchr("\"*/:<>?\\|", c)) c = c == ':' || c == '/' || c == '\\' || c == '|' ? '-' : ' ';
			if (c == ' ' && (o.empty() || o.back() == ' ')) continue;
			o += (char)c;
		}
		// cut at a character's edge (a continuation byte is 10xxxxxx)
		if (o.size() > maxBytes)
		{
			size_t cut = maxBytes;
			while (cut > 0 && ((unsigned char)o[cut] & 0xC0) == 0x80) cut--;
			o.resize(cut);
		}
		while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
		size_t a = 0;
		while (a < o.size() && (o[a] == ' ' || o[a] == '.')) a++;
		return o.substr(a);
	}

	std::string catalogBookName(const std::string& title, const std::string& author, const std::string& ext)
	{
		std::string t = exfatName(title, 100), a = exfatName(author, 60);
		if (t.empty()) t = "Book";
		std::string name = a.empty() ? t : t + " - " + a;
		return ext.empty() ? name : name + "." + ext;
	}

	std::string catalogFolderName(const std::string& catalogName)
	{
		std::string n = exfatName(catalogName, 60);
		return n.empty() ? "Catalog" : n;
	}

	std::string freeFileName(const std::string& path, bool (*taken)(const std::string&))
	{
		if (!taken(path)) return path;
		size_t slash = path.rfind('/'), dot = path.rfind('.');
		if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) dot = path.size();
		for (int i = 2; i < 1000; i++)
		{
			std::string p = path.substr(0, dot) + " (" + std::to_string(i) + ")" + path.substr(dot);
			if (!taken(p)) return p;
		}
		return path;
	}

	// ---- where it goes ----

	std::string chooseBooksFolder(const std::vector<BooksPlace>& places, const std::string& firstCard)
	{
		const BooksPlace* best = nullptr;
		for (const BooksPlace& p : places)
			if (!best || p.freeBytes > best->freeBytes) best = &p;
		if (best) return best->folder;
		if (firstCard.empty()) return "";
		return (firstCard == "/" ? "" : firstCard) + "/Books";
	}

	std::string coverCacheName(const std::string& url)
	{
		uint64_t h = 1469598103934665603ull;	// FNV-1a, 64 bit
		for (unsigned char c : url)
		{
			h ^= c;
			h *= 1099511628211ull;
		}
		char out[17];
		snprintf(out, sizeof out, "%016llx", (unsigned long long)h);
		return out;
	}

	// ---- what a catalog already gave the card ----

	bool CatalogGot::load(const std::string& file)
	{
		file_ = file;
		items_.clear();
		if (file.empty()) return false;
		std::ifstream in(file);
		if (!in) return false;
		std::string line;
		while (std::getline(in, line))
		{
			size_t tab = line.find('\t');
			if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) continue;
			items_.emplace_back(line.substr(0, tab), line.substr(tab + 1));
		}
		return true;
	}

	std::string CatalogGot::path(const std::string& entryId) const
	{
		for (auto& it : items_)
			if (it.first == entryId) return fileThere(it.second) ? it.second : "";
		return "";
	}

	std::vector<std::string> CatalogGot::paths(const std::string& entryId) const
	{
		std::vector<std::string> out;
		for (auto& it : items_)
			if (it.first == entryId && fileThere(it.second)) out.push_back(it.second);
		return out;
	}

	void CatalogGot::set(const std::string& entryId, const std::string& path)
	{
		// an id or a path with a tab or a line break in it would break the file: such a book is just not marked
		if (entryId.empty() || entryId.find_first_of("\t\r\n") != std::string::npos ||
		    path.find_first_of("\t\r\n") != std::string::npos)
			return;
		for (auto& it : items_)
			if (it.first == entryId && it.second == path) return;
		items_.emplace_back(entryId, path);
		save();
	}

	void CatalogGot::drop(const std::string& entryId)
	{
		size_t before = items_.size();
		items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const auto& it) { return it.first == entryId; }),
			items_.end());
		if (items_.size() != before) save();
	}

	int CatalogGot::count() const
	{
		int n = 0;
		for (auto& it : items_)
			if (fileThere(it.second)) n++;
		return n;
	}

	void CatalogGot::save() const
	{
		if (file_.empty()) return;
		std::string text;
		for (auto& it : items_) text += it.first + "\t" + it.second + "\n";
		writeFileDurable(file_, text);
	}
}
