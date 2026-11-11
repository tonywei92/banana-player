#include "mangaservers.h"
#include "netpath.h"	// newServerId: the ids the network folders use
#include "secret.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace bp
{
	namespace
	{
		const char* HEADER = "# banana-player manga servers v1";
		const size_t MAX_NAME = 40, MAX_URL = 2048;

		// A field's tabs, newlines and backslashes, so a line stays a line and a field stays a field (as in the
		// reader's other TSV files).
		std::string escapeField(const std::string& s)
		{
			std::string o;
			o.reserve(s.size());
			for (char c : s)
			{
				if (c == '\t') o += "\\t";
				else if (c == '\n') o += "\\n";
				else if (c == '\r') o += "\\r";
				else if (c == '\\') o += "\\\\";
				else o += c;
			}
			return o;
		}

		std::string unescapeField(const std::string& s)
		{
			std::string o;
			o.reserve(s.size());
			for (size_t i = 0; i < s.size(); i++)
			{
				if (s[i] != '\\' || i + 1 >= s.size())
				{
					o += s[i];
					continue;
				}
				char n = s[++i];
				if (n == 't') o += '\t';
				else if (n == 'n') o += '\n';
				else if (n == 'r') o += '\r';
				else if (n == '\\') o += '\\';
				else
				{
					o += '\\';	// an escape we don't know stays as it was written
					o += n;
				}
			}
			return o;
		}

		std::vector<std::string> splitTabs(const std::string& line)
		{
			std::vector<std::string> f;
			size_t pos = 0;
			for (;;)
			{
				size_t t = line.find('\t', pos);
				if (t == std::string::npos)
				{
					f.push_back(line.substr(pos));
					break;
				}
				f.push_back(line.substr(pos, t - pos));
				pos = t + 1;
			}
			return f;
		}

		std::string trim(const std::string& s)
		{
			size_t a = s.find_first_not_of(" \t\r\n");
			if (a == std::string::npos) return "";
			size_t b = s.find_last_not_of(" \t\r\n");
			return s.substr(a, b - a + 1);
		}

		std::string lowerAscii(std::string s)
		{
			for (auto& c : s)
				if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
			return s;
		}

		bool readWhole(const std::string& path, std::string& out)
		{
			FILE* f = fopen(path.c_str(), "rb");
			if (!f) return false;
			out.clear();
			char buf[4096];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
			fclose(f);
			return true;
		}

		// A name the reader shows: no control characters, and not too long to fit a row.
		bool validName(const std::string& name)
		{
			if (name.empty() || name.size() > MAX_NAME) return false;
			for (char c : name)
				if (c < ' ' || c == 127) return false;
			return true;
		}

		// The host part of a url: a name, an address or an IPv6 literal, with an optional port.
		bool validAuthority(const std::string& a)
		{
			if (a.empty()) return false;
			if (a.find('@') != std::string::npos) return false;	// credentials are never kept
			std::string host = a, port;
			bool hasPort = false;
			if (a[0] == '[')	// an IPv6 literal: [::1] or [::1]:8080
			{
				size_t close = a.find(']');
				if (close == std::string::npos) return false;
				host = a.substr(0, close + 1);
				std::string tail = a.substr(close + 1);
				if (!tail.empty())
				{
					if (tail[0] != ':') return false;
					port = tail.substr(1);
					hasPort = true;
				}
			}
			else
			{
				size_t colon = a.rfind(':');
				if (colon != std::string::npos)
				{
					host = a.substr(0, colon);
					port = a.substr(colon + 1);
					hasPort = true;
				}
				for (char c : host)
				{
					bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
						c == '.' || c == '-' || c == '_' || c == '%' || (unsigned char)c >= 0x80;
					if (!ok) return false;	// a character a host can't have
				}
			}
			if (host.empty()) return false;
			if (hasPort)
			{
				if (port.empty() || port.size() > 5) return false;
				for (char c : port)
					if (c < '0' || c > '9') return false;
			}
			return true;
		}

		// The servers we keep: a known kind, a name of 1..40 characters and a url we can reach. The name is trimmed
		// and the url loses its credentials: a plain password is never kept anywhere. A Kavita address's api key is
		// taken out of the url here and handed back for sealing: it never stays in the address.
		bool prepare(MangaServer& s, std::string& urlKey)
		{
			urlKey.clear();
			s.kind = lowerAscii(trim(s.kind));
			if (!isMangaServerKind(s.kind)) return false;
			s.name = trim(s.name);
			if (!validName(s.name)) return false;
			s.user = trim(s.user);
			for (char c : s.user)
				if (c < ' ' || c == 127) return false;	// it goes into a header later on
			s.url = trim(s.url);
			s.url = withoutUrlCredentials(s.url);
			if (!isMangaServerUrl(s.url)) return false;
			if (s.kind == "kavita")
			{
				std::string stored, key;
				if (readKavitaUrl(s.url, stored, key) && isMangaServerUrl(stored))
				{
					s.url = stored;
					urlKey = key;
				}
			}
			return true;
		}

		// A sealed password is only ours when it opens with our key: plain text must never reach the file.
		bool opensWith(const std::string& sealed, const std::string& key)
		{
			if (sealed.empty() || key.empty()) return false;
			std::string plain;
			return openSecret(sealed, key, plain);
		}

		// The list's order: by name, the way the Browse page shows them.
		bool nameLess(const MangaServer& a, const MangaServer& b)
		{
			std::string na = lowerAscii(a.name), nb = lowerAscii(b.name);
			if (na != nb) return na < nb;
			if (a.name != b.name) return a.name < b.name;
			return a.id < b.id;
		}

		// Written beside and renamed over, readable by its owner only: the file holds sealed passwords.
		bool writePrivate(const std::string& path, const std::string& text)
		{
			std::string tmp = path + ".tmp";
			{
				std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
				if (!out) return false;
				out.write(text.data(), std::streamsize(text.size()));
				out.flush();
				if (!out)
				{
					unlink(tmp.c_str());
					return false;
				}
			}
			chmod(tmp.c_str(), 0600);
			if (rename(tmp.c_str(), path.c_str()) != 0)
			{
				unlink(tmp.c_str());
				return false;
			}
			return true;
		}
	}

	bool isMangaServerKind(const std::string& kind)
	{
		return kind == "komga" || kind == "kavita" || kind == "opds";
	}

	bool isMangaServerUrl(const std::string& url)
	{
		if (url.empty() || url.size() > MAX_URL) return false;
		for (char c : url)
			if (c <= ' ' || c == 127) return false;	// no spaces, no control characters
		size_t p = url.find("://");
		if (p == std::string::npos) return false;
		std::string scheme = lowerAscii(url.substr(0, p));
		if (scheme != "http" && scheme != "https") return false;
		std::string rest = url.substr(p + 3);
		size_t end = rest.find_first_of("/?#");
		return validAuthority(rest.substr(0, end));
	}

	std::string withoutUrlCredentials(const std::string& url)
	{
		size_t p = url.find("://");
		if (p == std::string::npos) return url;
		size_t start = p + 3;
		size_t end = url.find_first_of("/?#", start);
		std::string authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
		// the user name may itself hold an '@', so the last one ends the credentials
		size_t at = authority.rfind('@');
		if (at == std::string::npos) return url;
		std::string host = authority.substr(at + 1);
		return url.substr(0, start) + host + (end == std::string::npos ? "" : url.substr(end));
	}

	namespace
	{
		// Kavita's own OPDS path, in the spelling Kavita writes it in ("/api/Opds"): the segment the key follows.
		const char* const KAVITA_OPDS = "/api/opds/";

		// Kavita prints its OPDS address as …/api/opds/<key>: the key runs to the path's end or to the tail after
		// it. Matched without case, since Kavita itself writes "Opds".
		bool splitKavitaApiKey(const std::string& url, std::string& storedUrl, std::string& key)
		{
			storedUrl = url;
			key.clear();
			size_t at = lowerAscii(url).find(KAVITA_OPDS);
			if (at == std::string::npos) return false;
			size_t start = at + std::string(KAVITA_OPDS).size();
			size_t end = url.find_first_of("/?#", start);
			if (end == std::string::npos) end = url.size();
			std::string k = url.substr(start, end - start);
			if (k.empty() || k == "{key}") return false;	// no key, or already the placeholder
			key = k;
			storedUrl = url.substr(0, start) + "{key}" + url.substr(end);
			return true;
		}

		// An address that names Kavita's OPDS endpoint (or only the server) and no key yet: {key} goes where the key
		// belongs, so one typed in the form has a place to go. Any other address is left as it is.
		std::string kavitaOpdsUrl(const std::string& url)
		{
			size_t scheme = url.find("://");
			if (scheme == std::string::npos) return url;
			size_t start = scheme + 3;
			size_t authEnd = url.find_first_of("/?#", start);
			std::string base = url.substr(0, authEnd == std::string::npos ? url.size() : authEnd);
			if (authEnd == std::string::npos) return base + KAVITA_OPDS + "{key}";
			size_t pathEnd = url.find_first_of("?#", authEnd);
			std::string path = url.substr(authEnd, pathEnd == std::string::npos ? std::string::npos : pathEnd - authEnd);
			std::string tail = pathEnd == std::string::npos ? "" : url.substr(pathEnd);
			std::string lower = lowerAscii(path);
			if (lower == "/api/opds" || lower == "/api/opds/" || lower == "/" || lower.empty())
				return base + KAVITA_OPDS + "{key}" + tail;
			return url;
		}
	}

	bool readKavitaUrl(const std::string& url, std::string& storedUrl, std::string& key)
	{
		if (splitKavitaApiKey(url, storedUrl, key)) return true;
		std::string opds = kavitaOpdsUrl(url);
		if (opds == url) return false;
		storedUrl = opds;
		return true;
	}

	bool MangaServerStore::load(const std::string& stateDir)
	{
		dir_ = stateDir;
		servers_.clear();
		key_.clear();
		if (dir_.empty()) return false;
		std::string manga = dir_ + "/manga";
		mkdir(dir_.c_str(), 0755);
		mkdir(manga.c_str(), 0755);
		// the key file beside the servers, salted with the handheld's serial: a copy of the card alone opens nothing
		std::string fileKey;
		if (loadOrCreateKey(manga + "/servers.key", fileKey)) key_ = deriveKey(fileKey, deviceSerial());
		std::string text;
		if (readWhole(manga + "/servers.tsv", text)) parse(text);
		// an address saved while Kavita's key was still in it is migrated here: the key is sealed and the file
		// written back, so a plain key never stays on the card
		bool migrated = false;
		for (MangaServer& s : servers_)
		{
			if (s.kind != "kavita") continue;
			std::string stored, urlKey;
			readKavitaUrl(s.url, stored, urlKey);
			if (!urlKey.empty())
			{
				if (key_.empty()) continue;	// without a key it can't be sealed, and it must not stay in the url
				std::string sealed = sealSecret(urlKey, key_);
				if (sealed.empty()) continue;
				s.sealedPassword = sealed;
			}
			if (stored == s.url) continue;
			s.url = stored;
			migrated = true;
		}
		if (migrated) save();
		changes_++;	// what the reader shows is different now
		return true;
	}

	bool MangaServerStore::save() const
	{
		if (dir_.empty()) return false;
		std::string manga = dir_ + "/manga";
		mkdir(dir_.c_str(), 0755);
		mkdir(manga.c_str(), 0755);
		return writePrivate(manga + "/servers.tsv", serialize());
	}

	std::string MangaServerStore::add(MangaServer s)
	{
		std::string urlKey;
		if (!prepare(s, urlKey)) return "";
		// a key out of a Kavita address is sealed like a typed one: it never stays in the url in plain text
		if (!urlKey.empty())
		{
			if (key_.empty()) return "";
			std::string sealed = sealSecret(urlKey, key_);
			if (sealed.empty()) return "";
			s.sealedPassword = sealed;
		}
		if (s.id.empty()) s.id = newServerId();
		for (auto& o : servers_)
			if (o.id == s.id) return "";	// the id is taken: update() changes that one
		// a sealed password comes with the server only if it opens with our key: plain text never reaches the file
		if (!s.sealedPassword.empty() && !opensWith(s.sealedPassword, key_)) s.sealedPassword.clear();
		servers_.push_back(s);
		changes_++;
		save();
		return s.id;
	}

	bool MangaServerStore::update(MangaServer s)
	{
		std::string urlKey;
		if (!prepare(s, urlKey)) return false;
		auto it = std::find_if(servers_.begin(), servers_.end(), [&](const MangaServer& o) { return o.id == s.id; });
		if (it == servers_.end()) return false;
		// the password stays unless a new sealed one comes with it, so an update that knows nothing of it doesn't
		// throw it away; a key out of the address is the newest one and replaces it
		std::string sealed = it->sealedPassword;
		if (!urlKey.empty())
		{
			if (key_.empty()) return false;
			sealed = sealSecret(urlKey, key_);
			if (sealed.empty()) return false;
		}
		else if (!s.sealedPassword.empty() && opensWith(s.sealedPassword, key_)) sealed = s.sealedPassword;
		s.sealedPassword = sealed;
		*it = s;
		changes_++;
		save();
		return true;
	}

	void MangaServerStore::remove(const std::string& id)
	{
		size_t before = servers_.size();
		servers_.erase(std::remove_if(servers_.begin(), servers_.end(),
					      [&](const MangaServer& s) { return s.id == id; }),
			servers_.end());
		if (servers_.size() == before) return;	// nothing was there: nothing changed
		changes_++;
		save();
	}

	const MangaServer* MangaServerStore::find(const std::string& id) const
	{
		for (auto& s : servers_)
			if (s.id == id) return &s;
		return nullptr;
	}

	std::vector<MangaServer> MangaServerStore::list() const
	{
		std::vector<MangaServer> out = servers_;
		std::sort(out.begin(), out.end(), nameLess);
		return out;
	}

	std::string MangaServerStore::password(const std::string& id) const
	{
		const MangaServer* s = find(id);
		if (!s || s->sealedPassword.empty() || key_.empty()) return "";
		std::string plain;
		if (!openSecret(s->sealedPassword, key_, plain)) return "";
		return plain;
	}

	bool MangaServerStore::setPassword(const std::string& id, const std::string& plain)
	{
		MangaServer* s = nullptr;
		for (auto& o : servers_)
			if (o.id == id)
			{
				s = &o;
				break;
			}
		if (!s) return false;
		std::string sealed;
		if (!plain.empty())
		{
			if (key_.empty()) return false;	// without a key it can't be sealed, and a plain password is never kept
			sealed = sealSecret(plain, key_);
			if (sealed.empty()) return false;
		}
		if (s->sealedPassword == sealed) return true;	// nothing changed
		s->sealedPassword = sealed;
		changes_++;
		save();
		return true;
	}

	void MangaServerStore::parse(const std::string& text)
	{
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty() || line[0] == '#') continue;
			std::vector<std::string> f = splitTabs(line);
			// a later version may add columns after these; a line missing one of ours is broken
			if (f.size() < 6) continue;
			MangaServer s;
			s.id = unescapeField(f[0]);
			s.kind = lowerAscii(unescapeField(f[1]));
			s.name = trim(unescapeField(f[2]));
			s.url = trim(unescapeField(f[3]));
			s.user = trim(unescapeField(f[4]));
			s.sealedPassword = unescapeField(f[5]);
			// a url with credentials in it is skipped, not tidied: a credential is never kept
			if (s.id.empty() || !isMangaServerKind(s.kind) || !validName(s.name) || !isMangaServerUrl(s.url)) continue;
			bool known = false;
			for (auto& o : servers_)
				if (o.id == s.id)
				{
					o = s;
					known = true;
					break;
				}
			if (!known) servers_.push_back(s);
		}
	}

	std::string MangaServerStore::serialize() const
	{
		std::vector<MangaServer> sorted = list();
		std::string out = HEADER;
		out += "\n";
		for (const auto& s : sorted)
			out += escapeField(s.id) + "\t" + escapeField(s.kind) + "\t" + escapeField(s.name) + "\t" +
				escapeField(s.url) + "\t" + escapeField(s.user) + "\t" + escapeField(s.sealedPassword) + "\n";
		return out;
	}
}
