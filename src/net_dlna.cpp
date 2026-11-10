// DLNA / UPnP media servers (Plex, Jellyfin, Synology, minidlna, Windows Media Player...) as network folders.
//
// The server is found with SSDP (an M-SEARCH for a MediaServer: sent straight to the host, and to the multicast group
// as well, as some servers answer only one of them) or given as a description URL ("http://host:port/desc.xml" in
// the Server field). Its ContentDirectory service lists the library: a folder is a container, browsed with SOAP
// (Browse, BrowseDirectChildren) and answered as DIDL-Lite; a file is an item whose <res> is a plain HTTP URL, read
// with Range requests. A DLNA server has no sign-in, so the form's user and password aren't used.
//
// DLNA addresses things by object id and the player's paths are names: "/Movies/Alien.mkv" is walked from the root
// container ("0") by title. The names that were listed are kept (the worker's backend lists a folder and then opens
// from it); a backend made for one file (mpv's) finds it by walking the path. Two entries of one title in a folder
// get " (2)", " (3)", so a path names one thing. A title with no file extension gets one from the file's type, as
// the browser tells media by its extension.
#include "net.h"
#include "playlist.h"
#include "netwait.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <map>
#include <mutex>
#include <set>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <vector>
#include <curl/curl.h>

namespace bp
{
	namespace
	{
		using netwait::Busy;
		using netwait::Canceller;
		using netwait::nowMs;

		bool transient(NetError e) { return e == NetError::Gone || e == NetError::Timeout; }

		// ---- a small XML reader: elements, attributes, text; namespace prefixes dropped; entities decoded ----

		struct XNode
		{
			std::string name, text;
			std::map<std::string, std::string> attr;
			std::vector<XNode> kids;

			const XNode* child(const char* n) const
			{
				for (const XNode& k : kids)
					if (k.name == n) return &k;
				return nullptr;
			}
			std::string childText(const char* n) const
			{
				const XNode* k = child(n);
				return k ? k->text : std::string();
			}
			void find(const char* n, std::vector<const XNode*>& out) const
			{
				for (const XNode& k : kids)
				{
					if (k.name == n) out.push_back(&k);
					k.find(n, out);
				}
			}
		};

		void appendUtf8(std::string& out, unsigned long cp)
		{
			if (cp < 0x80) out += (char)cp;
			else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
			else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
			else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
		}

		std::string decodeEntities(const std::string& s)
		{
			if (s.find('&') == std::string::npos) return s;
			std::string out;
			for (size_t i = 0; i < s.size(); i++)
			{
				if (s[i] != '&') { out += s[i]; continue; }
				size_t semi = s.find(';', i);
				if (semi == std::string::npos || semi - i > 10) { out += s[i]; continue; }
				std::string e = s.substr(i + 1, semi - i - 1);
				if (e == "amp") out += '&';
				else if (e == "lt") out += '<';
				else if (e == "gt") out += '>';
				else if (e == "quot") out += '"';
				else if (e == "apos") out += '\'';
				else if (!e.empty() && e[0] == '#')
				{
					unsigned long cp = e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? strtoul(e.c_str() + 2, nullptr, 16) : strtoul(e.c_str() + 1, nullptr, 10);
					if (cp) appendUtf8(out, cp);
				}
				else { out += s[i]; continue; }
				i = semi;
			}
			return out;
		}

		std::string localName(const std::string& n)
		{
			size_t c = n.rfind(':');
			return c == std::string::npos ? n : n.substr(c + 1);
		}

		class XmlParser
		{
		public:
			explicit XmlParser(const std::string& s) : s(s) {}

			bool parse(XNode& root)
			{
				skipMisc();
				if (i >= s.size() || s[i] != '<') return false;
				return element(root, 0);
			}

		private:
			const std::string& s;
			size_t i = 0;

			void skipMisc()
			{
				for (;;)
				{
					while (i < s.size() && isspace((unsigned char)s[i])) i++;
					if (s.compare(i, 2, "<?") == 0) { size_t e = s.find("?>", i); i = e == std::string::npos ? s.size() : e + 2; }
					else if (s.compare(i, 4, "<!--") == 0) { size_t e = s.find("-->", i); i = e == std::string::npos ? s.size() : e + 3; }
					else if (s.compare(i, 2, "<!") == 0 && s.compare(i, 9, "<![CDATA[") != 0) { size_t e = s.find('>', i); i = e == std::string::npos ? s.size() : e + 1; }
					else return;
				}
			}

			bool element(XNode& n, int depth)
			{
				if (depth > 64) return false;
				i++;	// '<'
				size_t a = i;
				while (i < s.size() && !isspace((unsigned char)s[i]) && s[i] != '>' && s[i] != '/') i++;
				n.name = localName(s.substr(a, i - a));
				for (;;)
				{
					while (i < s.size() && isspace((unsigned char)s[i])) i++;
					if (i >= s.size()) return false;
					if (s[i] == '/') { i++; if (i < s.size() && s[i] == '>') { i++; return true; } return false; }
					if (s[i] == '>') { i++; break; }
					size_t k = i;
					while (i < s.size() && s[i] != '=' && !isspace((unsigned char)s[i]) && s[i] != '>' && s[i] != '/') i++;
					std::string key = localName(s.substr(k, i - k));
					while (i < s.size() && isspace((unsigned char)s[i])) i++;
					if (i < s.size() && s[i] == '=')
					{
						i++;
						while (i < s.size() && isspace((unsigned char)s[i])) i++;
						if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) return false;
						char q = s[i++];
						size_t e = s.find(q, i);
						if (e == std::string::npos) return false;
						n.attr[key] = decodeEntities(s.substr(i, e - i));
						i = e + 1;
					}
				}
				for (;;)
				{
					size_t lt = s.find('<', i);
					if (lt == std::string::npos) return false;
					n.text += decodeEntities(s.substr(i, lt - i));
					i = lt;
					if (s.compare(i, 9, "<![CDATA[") == 0)
					{
						size_t e = s.find("]]>", i);
						if (e == std::string::npos) return false;
						n.text += s.substr(i + 9, e - i - 9);
						i = e + 3;
					}
					else if (s.compare(i, 4, "<!--") == 0) { size_t e = s.find("-->", i); if (e == std::string::npos) return false; i = e + 3; }
					else if (s.compare(i, 2, "<?") == 0) { size_t e = s.find("?>", i); if (e == std::string::npos) return false; i = e + 2; }
					else if (s.compare(i, 2, "</") == 0)
					{
						size_t e = s.find('>', i);
						if (e == std::string::npos) return false;
						i = e + 1;
						return true;
					}
					else
					{
						n.kids.emplace_back();
						if (!element(n.kids.back(), depth + 1)) return false;
					}
				}
			}
		};

		std::string trim(const std::string& s)
		{
			size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
			return a == std::string::npos ? "" : s.substr(a, b - a + 1);
		}

		std::string lower(std::string s)
		{
			for (char& c : s) c = (char)tolower((unsigned char)c);
			return s;
		}

		std::string xmlEscape(const std::string& s)
		{
			std::string o;
			for (char c : s)
			{
				if (c == '&') o += "&amp;";
				else if (c == '<') o += "&lt;";
				else if (c == '>') o += "&gt;";
				else if (c == '"') o += "&quot;";
				else o += c;
			}
			return o;
		}

		// ---- URLs ----

		// "http://host:port/dir/file" -> origin "http://host:port", directory "/dir/"
		void splitUrl(const std::string& url, std::string& origin, std::string& dir)
		{
			size_t a = url.find("://");
			size_t slash = a == std::string::npos ? url.find('/') : url.find('/', a + 3);
			origin = slash == std::string::npos ? url : url.substr(0, slash);
			std::string path = slash == std::string::npos ? "/" : url.substr(slash);
			size_t q = path.find_first_of("?#");
			if (q != std::string::npos) path.resize(q);
			size_t last = path.rfind('/');
			dir = path.substr(0, last + 1);
		}

		std::string resolveUrl(const std::string& base, const std::string& ref)
		{
			if (ref.compare(0, 7, "http://") == 0 || ref.compare(0, 8, "https://") == 0) return ref;
			std::string origin, dir;
			splitUrl(base, origin, dir);
			if (!ref.empty() && ref[0] == '/') return origin + ref;
			return origin + dir + ref;
		}

		std::string hostOfUrl(const std::string& url)
		{
			size_t a = url.find("://");
			if (a == std::string::npos) return "";
			size_t b = url.find_first_of("/:", a + 3);
			return url.substr(a + 3, b == std::string::npos ? std::string::npos : b - a - 3);
		}

		// ---- HTTP through libcurl (blocking, with the timeout and a cancel flag) ----

		struct Reply
		{
			long status = 0;
			std::string body;
			std::string contentRange;
			int64_t contentLength = -1;
		};

		struct WriteCtx
		{
			std::string* out;
			size_t limit;	// stop after this many bytes (a ranged read from a server that sends it all)
			bool cut = false;
		};

		size_t onWrite(char* p, size_t sz, size_t n, void* user)
		{
			WriteCtx* w = (WriteCtx*)user;
			size_t got = sz * n;
			if (w->out->size() + got > w->limit)
			{
				size_t take = w->limit > w->out->size() ? w->limit - w->out->size() : 0;
				w->out->append(p, take);
				w->cut = true;
				return 0;	// ends the transfer: enough
			}
			w->out->append(p, got);
			return got;
		}

		size_t onHeader(char* p, size_t sz, size_t n, void* user)
		{
			Reply* r = (Reply*)user;
			std::string line(p, sz * n);
			std::string l = lower(line);
			if (l.compare(0, 14, "content-range:") == 0) r->contentRange = trim(line.substr(14));
			else if (l.compare(0, 15, "content-length:") == 0) r->contentLength = atoll(line.c_str() + 15);
			return sz * n;
		}

		int onProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
		{
			return ((Canceller*)user)->cancelled() ? 1 : 0;
		}

		NetResult curlFailure(CURLcode code, const char* errbuf, const std::string& what, const std::string& host, bool cancelled)
		{
			NetResult r;
			r.detail = what + ": " + (errbuf && *errbuf ? errbuf : curl_easy_strerror(code));
			if (cancelled || code == CURLE_ABORTED_BY_CALLBACK) r.error = NetError::Cancelled;
			else if (code == CURLE_OPERATION_TIMEDOUT) r.error = NetError::Timeout;
			else if (code == CURLE_COULDNT_RESOLVE_HOST || code == CURLE_COULDNT_CONNECT) r.error = NetError::NotFound;
			else if (code == CURLE_RECV_ERROR || code == CURLE_SEND_ERROR || code == CURLE_GOT_NOTHING || code == CURLE_PARTIAL_FILE) r.error = NetError::Gone;
			else r.error = NetError::Other;
			(void)host;
			return r;
		}

		NetResult statusFailure(long status, const std::string& what)
		{
			NetResult r;
			r.detail = what + ": HTTP " + std::to_string(status);
			if (status == 401) r.error = NetError::Auth;
			else if (status == 403) r.error = NetError::NoAccess;
			else if (status == 404 || status == 410) r.error = NetError::NotFound;
			else if (status >= 500) r.error = NetError::Gone;
			else r.error = NetError::Other;
			return r;
		}

		// one request on a handle; limit: the most body bytes wanted. The handle keeps its connection for the next one
		NetResult httpRequest(CURL* c, Canceller& waiter, int timeoutMs, const std::string& url, const std::vector<std::string>& headers,
			const std::string* post, const char* method, Reply& reply, size_t limit = 64u << 20)
		{
			curl_easy_reset(c);
			char errbuf[CURL_ERROR_SIZE] = {};
			WriteCtx w{ &reply.body, limit };
			curl_slist* list = nullptr;
			for (const std::string& h : headers) list = curl_slist_append(list, h.c_str());
			curl_easy_setopt(c, CURLOPT_URL, url.c_str());
			curl_easy_setopt(c, CURLOPT_HTTPHEADER, list);
			curl_easy_setopt(c, CURLOPT_USERAGENT, "BananaPlayer (UPnP/1.0)");
			curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
			// item URLs come from the server: http(s) only, so a misbehaving one can't point at file:// and the like (nor can a redirect)
#if LIBCURL_VERSION_NUM >= 0x075500
			curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
			curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
			curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
			curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
			curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, (long)timeoutMs);
			curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);	// nothing at all for the timeout: give up
			curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, (long)std::max(1, timeoutMs / 1000));
			curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
			curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onWrite);
			curl_easy_setopt(c, CURLOPT_WRITEDATA, &w);
			curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, onHeader);
			curl_easy_setopt(c, CURLOPT_HEADERDATA, &reply);
			curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
			curl_easy_setopt(c, CURLOPT_XFERINFODATA, &waiter);
			if (post)
			{
				curl_easy_setopt(c, CURLOPT_POST, 1L);
				curl_easy_setopt(c, CURLOPT_POSTFIELDS, post->c_str());
				curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)post->size());
			}
			if (method && !strcmp(method, "HEAD")) curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
			CURLcode code = curl_easy_perform(c);
			curl_slist_free_all(list);
			if (code == CURLE_WRITE_ERROR && w.cut) code = CURLE_OK;
			if (code != CURLE_OK) return curlFailure(code, errbuf, "Requesting " + url, hostOfUrl(url), waiter.cancelled());
			curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &reply.status);
			return NetResult();
		}

		// ---- SSDP ----

		// What the sockets and descriptions say about a server found on the network
		struct Found
		{
			std::string location;
			std::string ip;
			bool mediaServer = false;	// its ST/NT says MediaServer (not an IGD, a renderer, another root device)
		};

		bool resolveV4(const std::string& host, std::string& ip)
		{
			addrinfo hints = {}, *res = nullptr;
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_DGRAM;
			if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
			char buf[64] = {};
			inet_ntop(AF_INET, &((sockaddr_in*)res->ai_addr)->sin_addr, buf, sizeof buf);
			freeaddrinfo(res);
			ip = buf;
			return true;
		}

		std::string headerValue(const std::string& reply, const char* name)
		{
			std::string low = lower(reply), key = std::string("\n") + name + ":";
			size_t p = low.find(key);
			if (p == std::string::npos) return "";
			size_t e = reply.find('\n', p + 1);
			return trim(reply.substr(p + key.size(), e == std::string::npos ? std::string::npos : e - p - key.size()));
		}

		// M-SEARCH for media servers: to wantIp's port 1900 (if given) and to the multicast group; the answers within
		// timeoutMs. wantIp "": all of them. Returns the LOCATIONs (one per server address, first answer)
		std::vector<Found> ssdpSearch(const std::string& wantIp, int timeoutMs, Canceller& waiter)
		{
			std::vector<Found> found;
			int fd = socket(AF_INET, SOCK_DGRAM, 0);
			if (fd < 0) return found;
			fcntl(fd, F_SETFD, FD_CLOEXEC);
			unsigned char ttl = 2;
			setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
			sockaddr_in any = {};
			any.sin_family = AF_INET;
			bind(fd, (sockaddr*)&any, sizeof any);
			const char* targets[] = { "urn:schemas-upnp-org:device:MediaServer:1", "upnp:rootdevice" };
			for (const char* st : targets)
			{
				if (!wantIp.empty() == false && st != targets[0]) break;	// a search of the whole network: media servers only
				std::string msg = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") + st + "\r\n\r\n";
				sockaddr_in to = {};
				to.sin_family = AF_INET;
				to.sin_port = htons(1900);
				inet_pton(AF_INET, "239.255.255.250", &to.sin_addr);
				sendto(fd, msg.data(), msg.size(), 0, (sockaddr*)&to, sizeof to);
				if (!wantIp.empty() && inet_pton(AF_INET, wantIp.c_str(), &to.sin_addr) == 1)
					sendto(fd, msg.data(), msg.size(), 0, (sockaddr*)&to, sizeof to);
			}
			int64_t until = nowMs() + timeoutMs;
			while (!waiter.cancelled())
			{
				int64_t left = until - nowMs();
				if (left <= 0) break;
				pollfd p[2] = { { fd, POLLIN, 0 }, { waiter.fd(), POLLIN, 0 } };
				if (poll(p, waiter.fd() >= 0 ? 2 : 1, (int)std::min<int64_t>(left, 200)) <= 0) continue;
				if (!(p[0].revents & POLLIN)) continue;
				char buf[2048];
				sockaddr_in from = {};
				socklen_t fl = sizeof from;
				ssize_t n = recvfrom(fd, buf, sizeof buf - 1, 0, (sockaddr*)&from, &fl);
				if (n <= 0) continue;
				buf[n] = 0;
				std::string reply(buf, (size_t)n);
				std::string loc = headerValue("\n" + reply, "location");
				if (loc.empty()) continue;
				char ip[64] = {};
				inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
				if (!wantIp.empty() && wantIp != ip && hostOfUrl(loc) != wantIp) continue;
				std::string st = lower(headerValue("\n" + reply, "st"));
				bool media = st.find("mediaserver") != std::string::npos;
				if (wantIp.empty() && !media) continue;
				bool dup = false;
				for (Found& f : found)
					if (f.location == loc) { dup = true; f.mediaServer = f.mediaServer || media; }
				if (!dup) found.push_back({ loc, ip, media });
				// an address asked for: a MediaServer's answer is the one wanted (others are kept, to be tried after it)
				if (!wantIp.empty() && media) break;
			}
			close(fd);
			std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.mediaServer && !b.mediaServer; });
			return found;
		}

		// ---- the ContentDirectory ----

		struct Item
		{
			std::string title, url, mime;
			bool dir = false;
			int64_t size = -1;
			std::string id;
		};

		std::string extensionFor(const Item& it)
		{
			std::string m = lower(it.mime);
			size_t semi = m.find(';');
			if (semi != std::string::npos) m.resize(semi);
			static const struct { const char* mime; const char* ext; } map[] = {
				{ "video/mp4", ".mp4" }, { "video/x-matroska", ".mkv" }, { "video/mkv", ".mkv" }, { "video/x-msvideo", ".avi" }, { "video/avi", ".avi" },
				{ "video/mpeg", ".mpg" }, { "video/mp2t", ".ts" }, { "video/quicktime", ".mov" }, { "video/x-ms-wmv", ".wmv" }, { "video/webm", ".webm" },
				{ "video/x-flv", ".flv" }, { "video/3gpp", ".3gp" }, { "audio/mpeg", ".mp3" }, { "audio/mp3", ".mp3" }, { "audio/flac", ".flac" },
				{ "audio/x-flac", ".flac" }, { "audio/mp4", ".m4a" }, { "audio/x-m4a", ".m4a" }, { "audio/aac", ".aac" }, { "audio/ogg", ".ogg" },
				{ "audio/x-ogg", ".ogg" }, { "audio/wav", ".wav" }, { "audio/x-wav", ".wav" }, { "audio/l16", ".wav" }, { "audio/x-ms-wma", ".wma" },
				{ "audio/opus", ".opus" }, { "image/jpeg", ".jpg" }, { "image/png", ".png" }, { "image/gif", ".gif" }, { "image/bmp", ".bmp" },
			};
			for (const auto& e : map)
				if (m == e.mime) return e.ext;
			// the file's own name, from the URL, when it has one
			std::string path = it.url.substr(0, it.url.find_first_of("?#"));
			size_t slash = path.rfind('/'), dot = path.rfind('.');
			if (dot != std::string::npos && slash != std::string::npos && dot > slash && path.size() - dot <= 6) return lower(path.substr(dot));
			if (m.compare(0, 6, "video/") == 0) return ".mkv";
			if (m.compare(0, 6, "audio/") == 0) return ".mp3";
			return "";
		}

		std::string cleanName(std::string s)
		{
			for (char& c : s)
				if (c == '/' || c == '\\' || (unsigned char)c < 32) c = '-';
			s = trim(s);
			while (!s.empty() && s[0] == '.') s.erase(0, 1);
			return s.empty() ? "Untitled" : s;
		}

		class DlnaSession
		{
		public:
			DlnaSession(const NetServer& server, int timeoutMs) : server(server), timeoutMs(timeoutMs)
			{
				signal(SIGPIPE, SIG_IGN);
				curl = curl_easy_init();
			}
			~DlnaSession() { if (curl) curl_easy_cleanup(curl); }
			DlnaSession(const DlnaSession&) = delete;
			DlnaSession& operator=(const DlnaSession&) = delete;

			const NetServer server;
			const int timeoutMs;
			Canceller waiter;
			CURL* curl = nullptr;
			std::string location, controlUrl, serviceType;

			// finds the device and its ContentDirectory
			NetResult connect()
			{
				if (!controlUrl.empty()) return NetResult();
				Busy busy(waiter);
				std::string host = trim(server.host);
				std::vector<std::string> candidates;
				if (host.compare(0, 7, "http://") == 0 || host.compare(0, 8, "https://") == 0) candidates.push_back(host);
				else
				{
					std::string ip;
					if (!resolveV4(host, ip))
					{
						NetResult r;
						r.error = NetError::NotFound;
						r.detail = "Can't find " + host;
						return r;
					}
					for (const Found& f : ssdpSearch(ip, std::min(timeoutMs, 3000), waiter)) candidates.push_back(f.location);
					if (waiter.cancelled()) return cancelled("Looking for the media server");
					if (server.port > 0)
					{
						std::string base = "http://" + (host.find(':') != std::string::npos ? "[" + host + "]" : host) + ":" + std::to_string(server.port);
						for (const char* p : { "/DeviceDescription.xml", "/rootDesc.xml", "/description.xml", "/dmr/DeviceDescription.xml", "/MediaServer.xml" })
							candidates.push_back(base + p);
					}
				}
				if (candidates.empty())
				{
					NetResult r;
					r.error = NetError::NotFound;
					r.detail = "No DLNA media server answered at " + host;
					return r;
				}
				NetResult last;
				for (const std::string& url : candidates)
				{
					Reply reply;
					NetResult r = httpRequest(curl, waiter, timeoutMs, url, {}, nullptr, nullptr, reply, 1u << 20);
					if (!r.ok()) { last = r; if (r.error == NetError::Cancelled) return r; continue; }
					if (reply.status != 200) { last = statusFailure(reply.status, "Description " + url); continue; }
					XNode root;
					if (!XmlParser(reply.body).parse(root)) { last.error = NetError::Other; last.detail = "Description " + url + ": not XML"; continue; }
					std::vector<const XNode*> services;
					root.find("service", services);
					for (const XNode* s : services)
					{
						std::string type = trim(s->childText("serviceType"));
						if (type.find(":ContentDirectory:") == std::string::npos) continue;
						std::string ctl = trim(s->childText("controlURL"));
						if (ctl.empty()) continue;
						std::string urlBase = trim(root.childText("URLBase"));
						controlUrl = resolveUrl(urlBase.empty() ? url : urlBase, ctl);
						serviceType = type;
						location = url;
						return NetResult();
					}
					last.error = NetError::NotFound;
					last.detail = "Description " + url + ": no ContentDirectory (not a media server)";
				}
				if (last.error == NetError::None) last.error = NetError::NotFound;
				return last;
			}

			NetResult cancelled(const std::string& what)
			{
				NetResult r;
				r.error = NetError::Cancelled;
				r.detail = what + ": cancelled";
				return r;
			}

			// the children of a container, all pages
			NetResult browse(const std::string& id, std::vector<Item>& out)
			{
				NetResult c = connect();
				if (!c.ok()) return c;
				Busy busy(waiter);
				out.clear();
				int total = -1;
				const int page = 5000;	// the server answers as many as it will: some (Plex) take seconds to build a list, whatever its length
				for (int start = 0; start < 50000;)
				{
					std::string body =
						"<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
						"<s:Body><u:Browse xmlns:u=\"" + serviceType + "\"><ObjectID>" + xmlEscape(id) + "</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag>"
						"<Filter>dc:title,upnp:class,res,res@size,res@protocolInfo</Filter><StartingIndex>" + std::to_string(start) + "</StartingIndex><RequestedCount>" + std::to_string(page) +
						"</RequestedCount><SortCriteria></SortCriteria></u:Browse></s:Body></s:Envelope>";
					std::vector<std::string> headers = { "Content-Type: text/xml; charset=\"utf-8\"", "SOAPACTION: \"" + serviceType + "#Browse\"" };
					NetResult r;
					Reply reply;
					for (int attempt = 0; attempt < 2; attempt++)
					{
						reply = Reply();
						r = httpRequest(curl, waiter, timeoutMs, controlUrl, headers, &body, nullptr, reply);
						if (r.ok() || !transient(r.error)) break;
						if (!waiter.sleep(400)) return cancelled("Browsing " + id);
					}
					if (!r.ok()) return r;
					if (reply.status == 500) return soapFault(reply.body, "Browse " + id);
					if (reply.status != 200) return statusFailure(reply.status, "Browse " + id);
					XNode env;
					if (!XmlParser(reply.body).parse(env))
					{
						NetResult e;
						e.error = NetError::Other;
						e.detail = "Browse " + id + ": the answer isn't XML";
						return e;
					}
					std::vector<const XNode*> results;
					env.find("Result", results);
					int returned = atoi(trim(findText(env, "NumberReturned")).c_str());
					int matches = atoi(trim(findText(env, "TotalMatches")).c_str());
					if (total < 0) total = matches;
					if (results.empty())
					{
						NetResult e;
						e.error = NetError::NotFound;
						e.detail = "Browse " + id + ": no such folder";
						return e;
					}
					XNode didl;
					if (!results[0]->text.empty() && XmlParser(results[0]->text).parse(didl)) parseDidl(didl, out);
					if (returned <= 0) break;
					start += returned;
					if (total > 0 && start >= total) break;
					if (total == 0 && returned < page) break;
				}
				return NetResult();
			}

			// a SOAP fault (UPnP answers an error as HTTP 500 with the code in the body): 701 "no such object" (an id that went
			// with a library refresh) is a missing folder, not a server that went away
			static NetResult soapFault(const std::string& body, const std::string& what)
			{
				NetResult r;
				XNode env;
				int code = 0;
				std::string text;
				if (XmlParser(body).parse(env))
				{
					code = atoi(trim(findText(env, "errorCode")).c_str());
					text = trim(findText(env, "errorDescription"));
				}
				if (code == 701 || code == 710) { r.error = NetError::NotFound; r.detail = what + ": no such folder"; }
				else
				{
					r.error = code ? NetError::Other : NetError::Gone;	// a 500 that says nothing: the server is struggling
					r.detail = what + ": the server says " + (code ? std::to_string(code) : std::string("HTTP 500")) + (text.empty() ? "" : " (" + text + ")");
				}
				return r;
			}

		private:
			static std::string findText(const XNode& n, const char* name)
			{
				std::vector<const XNode*> v;
				n.find(name, v);
				return v.empty() ? "" : v[0]->text;
			}

			static void parseDidl(const XNode& didl, std::vector<Item>& out)
			{
				for (const XNode& k : didl.kids)
				{
					bool container = k.name == "container", item = k.name == "item";
					if (!container && !item) continue;
					Item it;
					it.id = k.attr.count("id") ? k.attr.at("id") : "";
					it.title = k.childText("title");
					it.dir = container;
					if (item)
					{
						// the first <res> that is a plain HTTP download of something playable
						for (const XNode& r : k.kids)
						{
							if (r.name != "res") continue;
							std::string proto = r.attr.count("protocolInfo") ? r.attr.at("protocolInfo") : "";
							std::string low = lower(proto);
							if (low.compare(0, 9, "http-get:") != 0) continue;
							size_t c1 = proto.find(':'), c2 = proto.find(':', c1 + 1), c3 = proto.find(':', c2 + 1);
							std::string mime = c2 != std::string::npos && c3 != std::string::npos ? proto.substr(c2 + 1, c3 - c2 - 1) : "";
							std::string lm = lower(mime);
							if (!mime.empty() && lm.compare(0, 6, "video/") != 0 && lm.compare(0, 6, "audio/") != 0 && lm.compare(0, 6, "image/") != 0 && lm != "application/ogg") continue;
							it.url = trim(r.text);
							it.mime = mime;
							it.size = r.attr.count("size") ? atoll(r.attr.at("size").c_str()) : -1;
							break;
						}
						std::string lu = lower(it.url);
						if (lu.compare(0, 7, "http://") != 0 && lu.compare(0, 8, "https://") != 0) continue;	// (an empty one too)
					}
					out.push_back(std::move(it));
				}
			}
		};

		// a file read with Range requests, one block ahead
		class DlnaFile : public NetFile
		{
		public:
			DlnaFile(const NetServer& server, const std::string& url, int64_t size, int timeoutMs) : server(server), url(url), total(size), timeoutMs(timeoutMs)
			{
				signal(SIGPIPE, SIG_IGN);
				curl = curl_easy_init();
			}
			~DlnaFile() override { if (curl) curl_easy_cleanup(curl); }

			NetResult prepare()
			{
				if (total >= 0) return NetResult();
				Busy busy(waiter);
				Reply reply;
				NetResult r = httpRequest(curl, waiter, timeoutMs, url, headers(), nullptr, "HEAD", reply);
				if (!r.ok() && r.error == NetError::Cancelled) return last = r;
				// (a HEAD that failed outright is not the end: some servers drop the connection on it)
				if (r.ok() && reply.status == 200 && reply.contentLength >= 0) { total = reply.contentLength; return NetResult(); }
				// no HEAD: the first byte, and the total from Content-Range
				reply = Reply();
				std::vector<std::string> h = headers();
				h.push_back("Range: bytes=0-0");
				r = httpRequest(curl, waiter, timeoutMs, url, h, nullptr, nullptr, reply, 16);
				if (!r.ok()) return last = r;
				size_t slash = reply.contentRange.rfind('/');
				if (reply.status == 206 && slash != std::string::npos) { total = atoll(reply.contentRange.c_str() + slash + 1); return NetResult(); }
				// the server ignored the Range and answers with the whole file: its Content-Length is the size
				if (reply.status == 200 && reply.contentLength >= 0) { total = reply.contentLength; return NetResult(); }
				if (reply.status >= 400) return last = statusFailure(reply.status, "Opening " + url);
				last.error = NetError::Other;
				last.detail = "The server doesn't say how long the file is";
				return last;
			}

			int64_t read(char* buf, int64_t n, int64_t offset) override
			{
				if (n <= 0) return 0;
				if (total >= 0 && offset >= total) return 0;
				if (!(offset >= blockStart && offset < blockStart + (int64_t)block.size()))
				{
					if (!fetch(offset, std::max<int64_t>(n, kBlock))) return -1;
					if (block.empty()) return 0;
				}
				int64_t at = offset - blockStart;
				int64_t take = std::min<int64_t>(n, (int64_t)block.size() - at);
				memcpy(buf, block.data() + at, (size_t)take);
				return take;
			}
			int64_t size() const override { return total; }
			void cancel() override { waiter.cancel(); }
			NetResult lastResult() const override { return last; }

		private:
			static constexpr int64_t kBlock = 1 << 20;
			const NetServer server;
			const std::string url;
			int64_t total;
			const int timeoutMs;
			CURL* curl = nullptr;
			Canceller waiter;
			std::string block;
			int64_t blockStart = 0;
			NetResult last;

			std::vector<std::string> headers() const
			{
				return { "transferMode.dlna.org: Streaming", "getcontentFeatures.dlna.org: 1" };
			}

			bool fetch(int64_t offset, int64_t want)
			{
				if (total >= 0) want = std::min<int64_t>(want, total - offset);
				if (want <= 0) { block.clear(); blockStart = offset; return true; }
				Busy busy(waiter);
				for (int attempt = 0; attempt < 3; attempt++)
				{
					Reply reply;
					std::vector<std::string> h = headers();
					h.push_back("Range: bytes=" + std::to_string(offset) + "-" + std::to_string(offset + want - 1));
					NetResult r = httpRequest(curl, waiter, timeoutMs, url, h, nullptr, nullptr, reply, (size_t)want);
					if (r.ok() && reply.status == 206)
					{
						block = std::move(reply.body);
						blockStart = offset;
						return true;
					}
					if (r.ok() && reply.status == 200 && offset == 0)	// no ranges: the start of the file is what comes
					{
						block = std::move(reply.body);
						blockStart = 0;
						return true;
					}
					if (r.ok() && reply.status == 416) { block.clear(); blockStart = offset; return true; }	// past the end
					if (r.ok()) r = reply.status == 200 ? NetResult{ NetError::Other, "The server doesn't do ranged reads" } : statusFailure(reply.status, "Reading " + url);
					last = r;
					if (!transient(r.error) || attempt == 2) return false;
					if (!waiter.sleep(500 * (attempt + 1))) { last.error = NetError::Cancelled; return false; }
				}
				return false;
			}
		};

		// Folder listings shared by every backend of the process: the browser's worker lists a folder, and mpv's backend
		// for the file picked in it finds the file there instead of listing the folder again (a long one takes seconds on
		// some servers). Kept ten minutes; a listing shown to the user is always fresh and renews it.
		struct SharedListings
		{
			std::mutex lock;
			std::map<std::string, std::pair<int64_t, std::vector<Item>>> folders;
			static SharedListings& get() { static SharedListings s; return s; }
			bool find(const std::string& key, std::vector<Item>& out)
			{
				std::lock_guard<std::mutex> g(lock);
				auto it = folders.find(key);
				if (it == folders.end() || nowMs() - it->second.first > 10 * 60 * 1000) return false;
				out = it->second.second;
				return true;
			}
			void forget(const std::string& keyBase)	// everything kept for one server
			{
				std::lock_guard<std::mutex> g(lock);
				for (auto it = folders.begin(); it != folders.end();)
					it = it->first.compare(0, keyBase.size(), keyBase) == 0 ? folders.erase(it) : std::next(it);
			}
			void put(const std::string& key, const std::vector<Item>& v)
			{
				std::lock_guard<std::mutex> g(lock);
				if (folders.size() > 200) folders.clear();
				folders[key] = { nowMs(), v };
			}
		};

		class DlnaBackend : public NetBackend
		{
		public:
			DlnaBackend(const NetServer& server, int timeoutMs) : s(server, timeoutMs) {}

			NetResult connect() override
			{
				NetResult r = s.connect();
				if (!r.ok()) return r;
				std::vector<Item> root;
				return s.browse("0", root);
			}

			NetResult list(const std::string& path, std::vector<NetEntry>& out) override
			{
				std::vector<Item> items;
				NetResult r = children(path, items, true);
				if (!r.ok()) return r;
				out.clear();
				for (const Item& it : items) out.push_back({ it.title, it.dir, it.size });
				return r;
			}

			std::unique_ptr<NetFile> open(const std::string& path, NetResult& result) override
			{
				std::string dir = path.substr(0, path.rfind('/'));
				if (dir.empty()) dir = "/";
				std::string name = path.substr(path.rfind('/') + 1);
				std::vector<Item> items;
				result = children(dir, items);
				if (!result.ok()) return nullptr;
				for (const Item& it : items)
				{
					if (it.dir || it.title != name) continue;
					auto f = std::make_unique<DlnaFile>(s.server, it.url, it.size, s.timeoutMs);
					{
						std::lock_guard<std::mutex> g(openLock);
						opening = f.get();
					}
					result = f->prepare();
					{
						std::lock_guard<std::mutex> g(openLock);
						opening = nullptr;
					}
					if (!result.ok()) return nullptr;
					return f;
				}
				result.error = NetError::NotFound;
				result.detail = "Opening " + path + ": no such file";
				return nullptr;
			}

			// (the file being opened has a canceller of its own: Next and quit must reach it too, as its HEAD and Range probes
			// can wait for the server for many seconds)
			void cancel() override
			{
				s.waiter.cancel();
				std::lock_guard<std::mutex> g(openLock);
				if (opening) opening->cancel();
			}

		private:
			DlnaSession s;
			std::mutex openLock;
			DlnaFile* opening = nullptr;
			std::map<std::string, std::vector<Item>> listed;	// a folder's path -> its entries (as named for the player)

			// a folder's entries, titled so each is one path. A listing kept from before may hold object ids the server has
			// since renumbered (Plex does, when its library is refreshed): when walking from kept listings ends in a
			// missing object or folder, everything kept for this server is dropped and the walk is made again, once
			NetResult children(const std::string& path, std::vector<Item>& out, bool fresh = false)
			{
				usedKept = false;
				NetResult r = walk(path, out, fresh);
				if (r.error == NetError::NotFound && usedKept)
				{
					listed.clear();
					SharedListings::get().forget(s.server.host + "|" + std::to_string(s.server.port) + "|");
					usedKept = false;
					r = walk(path, out, fresh);
				}
				return r;
			}

			bool usedKept = false;	// the walk in progress took a listing that was kept, not asked of the server just now

			// walked from the root, each level once
			NetResult walk(const std::string& path, std::vector<Item>& out, bool fresh)
			{
				const std::string keyBase = s.server.host + "|" + std::to_string(s.server.port) + "|";
				std::vector<std::string> segs;
				for (size_t a = 0; a < path.size();)
				{
					size_t b = path.find('/', a);
					if (b == std::string::npos) b = path.size();
					if (b > a) segs.push_back(path.substr(a, b - a));
					a = b + 1;
				}
				std::string at = "/", id = "0";
				for (size_t k = 0;; k++)
				{
					bool last = k == segs.size();
					auto it = listed.find(at);
					if (it != listed.end() && last && fresh) { listed.erase(it); it = listed.end(); }	// a listing for the user is asked of the server again
					else if (it != listed.end()) usedKept = true;
					if (it == listed.end())
					{
						std::vector<Item> named;
						if (!(last && fresh) && SharedListings::get().find(keyBase + at, named)) { it = listed.emplace(at, named).first; usedKept = true; }
						else
						{
							std::vector<Item> raw;
							NetResult r = s.browse(id, raw);
							if (!r.ok()) return r;
							named = name(raw);
							SharedListings::get().put(keyBase + at, named);
							it = listed.emplace(at, named).first;
						}
					}
					if (k == segs.size()) { out = it->second; return NetResult(); }
					const Item* next = nullptr;
					for (const Item& e : it->second)
						if (e.dir && e.title == segs[k]) { next = &e; break; }
					if (!next)
					{
						NetResult r;
						r.error = NetError::NotFound;
						r.detail = "Opening " + path + ": no such folder";
						return r;
					}
					id = next->id;
					at = at == "/" ? "/" + segs[k] : at + "/" + segs[k];
				}
			}

			// each entry's title as the player's name for it: one per entry, with an extension on a file
			static std::vector<Item> name(std::vector<Item> raw)
			{
				std::vector<Item> dirs, files;
				std::map<std::string, int> used;
				for (Item& it : raw)
				{
					std::string base = cleanName(it.title), ext;
					if (!it.dir && !isMediaFile(base)) ext = extensionFor(it);
					std::string candidate = base + ext;
					for (int n = 2; used.count(lower(candidate)); n++) candidate = base + " (" + std::to_string(n) + ")" + ext;
					used[lower(candidate)] = 1;	// (the new name too: "Alien", "Alien", "Alien (2)" must not give "Alien (2)" twice)
					it.title = candidate;
					(it.dir ? dirs : files).push_back(std::move(it));
				}
				dirs.insert(dirs.end(), files.begin(), files.end());
				return dirs;
			}
		};
	}

	std::unique_ptr<NetBackend> makeDlnaBackend(const NetServer& server, const std::string&, int timeoutMs)
	{
		return std::make_unique<DlnaBackend>(server, timeoutMs);
	}
}
