#include "mangaworker.h"
#include "challenge.h"
#include "doh.h"
#include "netpath.h"
#include "netstore.h"
#include "webpath.h"
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <curl/curl.h>
#include <deque>
#include <dirent.h>
#include <fstream>
#include <ifaddrs.h>
#include <iterator>
#include <map>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace bp
{
	namespace
	{
		constexpr size_t kMaxBody = 8u * 1024u * 1024u;	// a script's request (5.4); images are fetched elsewhere
		constexpr int kDefaultTimeout = 20;
		constexpr int kMaxTimeout = 30;
		constexpr long kConnectMs = 15000;
		constexpr long kMaxRedirects = 5;
		constexpr int kMaxRequestsPerCall = 40;	// 5.4; the host counts too, this is the socket's own guard
		constexpr double kDefaultIdle = 5 * 60;
		// The 5.4 limits for a call. The host's own defaults are the same; they are set here so the worker's numbers
		// show up in one place.
		constexpr double kCallSeconds = 30;
		constexpr uint64_t kCallInstructions = 200000000;
		constexpr size_t kCallMemory = 32u * 1024u * 1024u;
		// validateScript's budget: an install waits for this, so it is short and the script gets no network.
		constexpr double kCheckSeconds = 1;
		constexpr uint64_t kCheckInstructions = 5000000;

		using Clock = std::chrono::steady_clock;

		// mkdir -p, best effort: a prefs write that can't happen is reported by the write itself.
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

		// A remote chapter's copy is made beside its place and renamed when whole, so a stopped fetch never looks
		// like a chapter: the same half-written rule the page cache keeps.
		void removeTree(const std::string& path)
		{
			struct stat st;
			if (lstat(path.c_str(), &st) != 0) return;
			if (!S_ISDIR(st.st_mode))
			{
				unlink(path.c_str());
				return;
			}
			if (DIR* d = opendir(path.c_str()))
			{
				std::vector<std::string> subs;
				while (dirent* e = readdir(d))
				{
					std::string name = e->d_name;
					if (name == "." || name == "..") continue;
					subs.push_back(path + "/" + name);
				}
				closedir(d);
				for (const std::string& sub : subs) removeTree(sub);
			}
			rmdir(path.c_str());
		}

		struct NetCopy
		{
			bool ok = false;
			bool tooBig = false;
			std::string error;
			int64_t bytes = 0;
		};

		// One remote file into path, from start to end. abort is checked between chunks (a cancel from another
		// thread reaches the backend's own cancel call too). A chapter over the cap isn't copied at all.
		NetCopy copyNetFile(NetFile& file, const std::string& path, const std::atomic<bool>& abort)
		{
			NetCopy out;
			constexpr int64_t kMaxBytes = 4ll * 1024 * 1024 * 1024;
			int64_t known = file.size();
			if (known > kMaxBytes)
			{
				out.tooBig = true;
				out.error = "the file is too big";
				return out;
			}
			FILE* f = fopen(path.c_str(), "wb");
			if (!f)
			{
				out.error = "can't write the chapter";
				return out;
			}
			std::vector<char> buf(256 * 1024);
			int64_t off = 0;
			while (!abort.load())
			{
				int64_t n = file.read(buf.data(), (int64_t)buf.size(), off);
				if (n < 0)
				{
					out.error = file.lastResult().detail.empty() ? "the server stopped" : file.lastResult().detail;
					break;
				}
				if (n == 0)
				{
					out.ok = true;
					break;
				}
				if (off + n > kMaxBytes)
				{
					out.tooBig = true;
					out.error = "the file is too big";
					break;
				}
				if (fwrite(buf.data(), 1, (size_t)n, f) != (size_t)n)
				{
					out.error = "the card is full";
					break;
				}
				off += n;
			}
			if (!out.ok && out.error.empty()) out.error = "cancelled";
			if (fclose(f) != 0 && out.ok)
			{
				out.ok = false;
				out.error = "the card is full";
			}
			out.bytes = off;
			return out;
		}

		bool safeName(const std::string& name)
		{
			return !name.empty() && name != "." && name != ".." && name.find('/') == std::string::npos &&
				name.find('\\') == std::string::npos;
		}

		// The backend a job is inside now, so cancel() can end its call from the Ui's thread. Clearing it under the
		// worker's mutex is what keeps a cancel from calling into a backend the job has just freed: cancel() holds
		// that same mutex while it calls it.
		struct NetBusyGuard
		{
			std::atomic<NetBackend*>& slot;
			std::mutex& lock;
			NetBusyGuard(std::atomic<NetBackend*>& s, std::mutex& m, NetBackend* p) : slot(s), lock(m) { slot = p; }
			~NetBusyGuard()
			{
				std::lock_guard<std::mutex> g(lock);
				slot = nullptr;
			}
		};

		bool readWhole(const std::string& path, std::string& out)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in) return false;
			out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			return !in.bad();
		}

		// "abc123/example-reader" -> "abc123" / "example-reader"; the id is the script's own id, never the full one.
		std::string repoHashOf(const std::string& fullId)
		{
			size_t slash = fullId.find('/');
			return slash == std::string::npos ? fullId : fullId.substr(0, slash);
		}

		std::string sourceIdOf(const std::string& fullId)
		{
			size_t slash = fullId.find('/');
			return slash == std::string::npos ? fullId : fullId.substr(slash + 1);
		}

		// A source's prefs file name (5.4): the full id with its separator as a dash, since the state dir is a folder.
		std::string prefsNameOf(const std::string& fullId)
		{
			std::string name = fullId;
			std::replace(name.begin(), name.end(), '/', '-');
			return name + ".json";
		}

		bool iequals(const std::string& a, const char* b)
		{
			size_t n = std::char_traits<char>::length(b);
			if (a.size() != n) return false;
			for (size_t i = 0; i < n; i++)
				if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
			return true;
		}

		struct Sink
		{
			std::string body;
			std::map<std::string, std::string> headers;
			bool over = false;
		};

		size_t onWrite(char* data, size_t size, size_t nmemb, void* user)
		{
			Sink* s = (Sink*)user;
			size_t n = size * nmemb;
			// 0 aborts the transfer: the 8 MB cap is a limit of the contract, not a suggestion
			if (s->body.size() + n > kMaxBody)
			{
				s->over = true;
				return 0;
			}
			s->body.append(data, n);
			return n;
		}

		std::string trim(const std::string& s)
		{
			size_t a = 0, b = s.size();
			while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
			while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
			return s.substr(a, b - a);
		}

		// Duplicates (Set-Cookie) are joined, as the host's map has one value per name: the script's own use of a
		// header rarely needs the rest, and cookies are kept by curl's jar anyway.
		size_t onHeader(char* data, size_t size, size_t nmemb, void* user)
		{
			Sink* s = (Sink*)user;
			size_t n = size * nmemb;
			std::string line(data, n);
			size_t colon = line.find(':');
			if (colon != std::string::npos)
			{
				std::string name = trim(line.substr(0, colon));
				std::string value = trim(line.substr(colon + 1));
				if (!name.empty())
				{
					auto it = s->headers.find(name);
					if (it == s->headers.end()) s->headers[name] = value;
					else it->second += ", " + value;
				}
			}
			return n;
		}

		int onProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
		{
			std::atomic<bool>* cancel = (std::atomic<bool>*)user;
			return cancel && cancel->load() ? 1 : 0;
		}

		// Secure DNS (design 9): Cloudflare's DNS over HTTPS, reached at its IP literal (its certificate covers
		// 1.1.1.1) so the lookup itself never goes through the resolver that is blocking these sites.
		constexpr long kDohTimeoutMs = 4000, kDohConnectMs = 2500;

		struct DohClient
		{
			DohCache cache;
			std::atomic<uint16_t> ids{1};
		};

		struct DohFetch
		{
			bool ok = false;
			std::vector<std::string> addresses;
			uint32_t ttl = 0;
			std::string error;
		};

		// One query through Cloudflare; the answer itself is read by bpcore's parser.
		DohFetch dohPost(DohClient& doh, const std::string& host, uint16_t type)
		{
			DohFetch out;
			uint16_t id = doh.ids.fetch_add(1);
			std::vector<uint8_t> query = dohQuery(host, type, id);
			if (query.empty())
			{
				out.error = "the name can't be one in a DNS query";
				return out;
			}
			CURL* e = curl_easy_init();
			if (!e)
			{
				out.error = "couldn't make a request";
				return out;
			}
			Sink sink;
			char errbuf[CURL_ERROR_SIZE] = {};
			curl_slist* headers = nullptr;
			headers = curl_slist_append(headers, "Content-Type: application/dns-message");
			headers = curl_slist_append(headers, "Accept: application/dns-message");
			curl_easy_setopt(e, CURLOPT_URL, "https://1.1.1.1/dns-query");
			curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500	// 7.85.0: the string forms of the protocol masks
			curl_easy_setopt(e, CURLOPT_PROTOCOLS_STR, "https");
#else
			curl_easy_setopt(e, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif
			// The lookup must not be caught by a proxy that would resolve a name of its own: it goes to the literal.
			curl_easy_setopt(e, CURLOPT_PROXY, "");
			curl_easy_setopt(e, CURLOPT_NOPROXY, "*");
			curl_easy_setopt(e, CURLOPT_TIMEOUT_MS, kDohTimeoutMs);
			curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT_MS, kDohConnectMs);
			curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, 1L);
			curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, 2L);
			curl_easy_setopt(e, CURLOPT_POST, 1L);
			curl_easy_setopt(e, CURLOPT_POSTFIELDS, (const char*)query.data());
			curl_easy_setopt(e, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)query.size());
			if (headers) curl_easy_setopt(e, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, &onWrite);
			curl_easy_setopt(e, CURLOPT_WRITEDATA, &sink);
			curl_easy_setopt(e, CURLOPT_ERRORBUFFER, errbuf);
			CURLcode rc = curl_easy_perform(e);
			long status = 0;
			curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &status);
			curl_easy_cleanup(e);
			if (headers) curl_slist_free_all(headers);
			if (rc != CURLE_OK)
			{
				out.error = errbuf[0] ? errbuf : curl_easy_strerror(rc);
				return out;
			}
			if (status != 200)
			{
				out.error = "HTTP " + std::to_string(status);
				return out;
			}
			DohAnswer answer = parseDohAnswer(sink.body, id, host, type);
			if (answer.status == DohAnswer::Malformed || answer.status == DohAnswer::Truncated ||
				answer.status == DohAnswer::NameError)
			{
				out.error = answer.error.empty() ? "the answer can't be read" : answer.error;
				return out;
			}
			// NoRecords is a good answer: the name exists but has no address of this kind.
			out.ok = true;
			out.addresses = std::move(answer.addresses);
			out.ttl = answer.ttl;
			return out;
		}

		struct DohResult
		{
			bool ok = false;
			std::string error;
			std::vector<std::string> addresses;
		};

		// Both families, the cache first. One address of either is a good lookup; the caller then applies the same
		// public-address and own-address checks that the system's answer went through.
		DohResult dohResolve(DohClient& doh, const std::string& host)
		{
			DohResult out;
			double now = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
			std::string error;
			for (uint16_t type : {DOH_TYPE_A, DOH_TYPE_AAAA})
			{
				std::vector<std::string> cached;
				if (doh.cache.get(host, type, now, cached))
				{
					out.addresses.insert(out.addresses.end(), cached.begin(), cached.end());
					continue;
				}
				DohFetch found = dohPost(doh, host, type);
				if (!found.ok)
				{
					if (error.empty()) error = found.error;
					continue;
				}
				if (found.addresses.empty()) continue;
				doh.cache.put(host, type, found.addresses, found.ttl, now);
				out.addresses.insert(out.addresses.end(), found.addresses.begin(), found.addresses.end());
			}
			if (!out.addresses.empty())
			{
				out.ok = true;
				return out;
			}
			out.error = error.empty() ? "no addresses" : error;
			return out;
		}

		// One hop of a request (the URL the script gave, then each redirect): the name it asks for and the one
		// public address curl is allowed to connect to.
		struct Hop
		{
			std::string url, host, port, address;
		};

		std::string addressOf(const sockaddr* sa)
		{
			char buf[INET6_ADDRSTRLEN] = {};
			if (sa->sa_family == AF_INET)
			{
				if (!inet_ntop(AF_INET, &((const sockaddr_in*)sa)->sin_addr, buf, sizeof buf)) return "";
			}
			else if (sa->sa_family == AF_INET6)
			{
				if (!inet_ntop(AF_INET6, &((const sockaddr_in6*)sa)->sin6_addr, buf, sizeof buf)) return "";
			}
			return buf;
		}

		// Any of the card's own addresses, public or not: a script must not reach the handheld itself (5.4).
		bool ownAddress(const std::string& ip)
		{
			struct ifaddrs* list = nullptr;
			if (getifaddrs(&list) != 0) return false;
			bool own = false;
			for (struct ifaddrs* i = list; i; i = i->ifa_next)
			{
				if (!i->ifa_addr) continue;
				if (addressOf(i->ifa_addr) == ip)
				{
					own = true;
					break;
				}
			}
			freeifaddrs(list);
			return own;
		}

		// The canonical form of a literal host ("::1", "127.0.0.1"), or "" when it is a name. Brackets and a zone
		// are dropped, as publicAddress drops them too.
		std::string literalAddress(const std::string& hostIn)
		{
			std::string h = hostIn;
			if (h.size() >= 2 && h.front() == '[' && h.back() == ']') h = h.substr(1, h.size() - 2);
			h = h.substr(0, h.find('%'));
			in_addr a4;
			in6_addr a6;
			char out[INET6_ADDRSTRLEN] = {};
			if (inet_pton(AF_INET, h.c_str(), &a4) == 1)
				return inet_ntop(AF_INET, &a4, out, sizeof out) ? out : "";
			if (inet_pton(AF_INET6, h.c_str(), &a6) == 1)
				return inet_ntop(AF_INET6, &a6, out, sizeof out) ? out : "";
			return "";
		}

		// The host and port of an absolute http(s) URL; "" when the hop may be tried, otherwise the refusal.
		std::string parseUrl(const std::string& url, std::string& host, std::string& port)
		{
			CURLU* u = curl_url();
			if (!u) return "Blocked: out of memory";
			std::string scheme, user, pass;
			bool ok = curl_url_set(u, CURLUPART_URL, url.c_str(), 0) == CURLUE_OK;
			auto get = [&](CURLUPart part, std::string& out) {
				char* s = nullptr;
				if (ok && curl_url_get(u, part, &s, 0) == CURLUE_OK && s)
				{
					out = s;
					curl_free(s);
				}
			};
			if (ok)
			{
				get(CURLUPART_SCHEME, scheme);
				get(CURLUPART_HOST, host);
				get(CURLUPART_PORT, port);
				get(CURLUPART_USER, user);
				get(CURLUPART_PASSWORD, pass);
			}
			curl_url_cleanup(u);
			if (!ok || host.empty()) return "Blocked: not a valid URL";
			if (!iequals(scheme, "http") && !iequals(scheme, "https")) return "Blocked: only http and https";
			// Credentials in a URL would end up in a rendered page or a log: the source has headers for auth.
			if (!user.empty() || !pass.empty()) return "Blocked: a URL may not carry credentials";
			if (host.size() >= 2 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
			if (port.empty()) port = iequals(scheme, "https") ? "443" : "80";
			return "";
		}

		// 5.4's rule for one hop: http(s), no credentials, and a host with at least one public address that isn't
		// the card's own. The address is the one curl is pinned to; a name is never resolved by curl itself.
		std::string checkHop(const std::string& url, Hop& hop, DohClient* doh)
		{
			hop.url = url;
			std::string error = parseUrl(url, hop.host, hop.port);
			if (!error.empty()) return error;
			std::string literal = literalAddress(hop.host);
			if (!literal.empty())
			{
				if (!publicAddress(literal) || ownAddress(literal))
					return "Blocked: " + hop.host + " is not a public address";
				hop.address = literal;
				return "";
			}
			// Secure DNS: the name is asked of Cloudflare, not the resolver this network supplies, and the answer
			// is then held to the same rule as any other (a private address is refused below, not connected to).
			if (doh)
			{
				DohResult found = dohResolve(*doh, hop.host);
				if (!found.ok)
				{
					fprintf(stderr, "manga: DNS over HTTPS lookup of %s failed: %s\n", hop.host.c_str(),
						found.error.c_str());
					return "couldn't resolve " + hop.host;
				}
				for (const std::string& ip : found.addresses)
				{
					if (ip.empty() || !publicAddress(ip) || ownAddress(ip)) continue;
					hop.address = ip;
					return "";
				}
				return "Blocked: " + hop.host + " is not a public address";
			}
			struct addrinfo hints = {};
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			struct addrinfo* found = nullptr;
			if (getaddrinfo(hop.host.c_str(), hop.port.c_str(), &hints, &found) != 0)
				return "couldn't resolve " + hop.host;
			std::string address;
			for (struct addrinfo* a = found; a; a = a->ai_next)
			{
				std::string ip = addressOf(a->ai_addr);
				if (ip.empty() || !publicAddress(ip) || ownAddress(ip)) continue;
				address = ip;
				break;
			}
			freeaddrinfo(found);
			if (address.empty()) return "Blocked: " + hop.host + " is not a public address";
			hop.address = address;
			return "";
		}

		struct Easy
		{
			CURL* e = nullptr;
			curl_slist* list = nullptr;
			curl_slist* resolve = nullptr;	// the pin CURLOPT_RESOLVE points at; curl shares, not copies, the list
			~Easy()
			{
				// the handle goes first: the header list is not owned by curl, but its cleanup must not see it gone
				if (e) curl_easy_cleanup(e);
				if (list) curl_slist_free_all(list);
				if (resolve) curl_slist_free_all(resolve);
			}
			// a header line per given pair; empty names are skipped, as the fetcher does
			bool addHeaders(const std::map<std::string, std::string>& headers)
			{
				for (const auto& h : headers)
				{
					if (h.first.empty()) continue;
					std::string line = h.first + ": " + h.second;
					curl_slist* next = curl_slist_append(list, line.c_str());
					if (!next) return false;
					list = next;
				}
				return true;
			}
		};

		// One source as the thread holds it: its state (when open), its cookies and where it was last called.
		struct SourceState
		{
			std::string fullId, id, scriptPath, prefsPath, userAgent;
			std::unique_ptr<SourceHost> host;
			CURLSH* cookies = nullptr;	// owned by the worker, so cookies outlive an idle close
			std::atomic<bool>* cancel = nullptr;	// the worker's flag for the job running now
			DohClient* doh = nullptr;			// the worker's lookup and its cache (Secure DNS)
			std::atomic<bool>* secureDns = nullptr;	// the Manga setting, read here so a change reaches the next hop
			int requestsThisCall = 0;	// 5.4's 40, reset by the job (and by loading the script's top level)
			Clock::time_point lastUse;

			~SourceState() = default;

			bool cancelled() const { return cancel && cancel->load(); }

			// The injected HttpFn: one synchronous easy transfer on the worker's thread. It throws on failure, which
			// the host turns into a Lua error (mangasource.h's contract).
			HttpResult fetch(const std::string& method, const std::string& url,
				const std::map<std::string, std::string>& headers, const std::string& body, int timeoutSeconds);
		};

		HttpResult SourceState::fetch(const std::string& method, const std::string& url,
			const std::map<std::string, std::string>& headers, const std::string& body, int timeoutSeconds)
		{
			// A cancelled job must not open another socket: the script may have caught the first failure.
			if (cancelled()) throw std::runtime_error("the source was cancelled");
			if (++requestsThisCall > kMaxRequestsPerCall)
				throw std::runtime_error("more than 40 requests in one call");
			int timeout = timeoutSeconds <= 0 ? kDefaultTimeout : timeoutSeconds;
			if (timeout > kMaxTimeout) timeout = kMaxTimeout;

			std::string currentUrl = url, currentMethod = method, currentBody = body;
			long redirects = 0;
			for (;;)
			{
				if (cancelled()) throw std::runtime_error("the source was cancelled");
				Hop hop;
				// Every hop is checked afresh, redirects included: curl neither resolves nor follows on its own.
				bool useDoh = doh && secureDns && secureDns->load();
				std::string blocked = checkHop(currentUrl, hop, useDoh ? doh : nullptr);
				if (!blocked.empty()) throw std::runtime_error(blocked);

				Easy easy;
				easy.e = curl_easy_init();
				if (!easy.e) throw std::runtime_error("couldn't make a request");
				CURL* e = easy.e;
				char errbuf[CURL_ERROR_SIZE] = {};
				Sink sink;
				curl_easy_setopt(e, CURLOPT_URL, hop.url.c_str());
				curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
				curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 0L);	// the next hop is resolved and pinned here too
#if LIBCURL_VERSION_NUM >= 0x075500	// 7.85.0: the string forms of the protocol masks
				curl_easy_setopt(e, CURLOPT_PROTOCOLS_STR, "http,https");
#else
				curl_easy_setopt(e, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
				// the environment's proxies could otherwise connect somewhere the pin never saw
				curl_easy_setopt(e, CURLOPT_PROXY, "");
				curl_easy_setopt(e, CURLOPT_NOPROXY, "*");
				curl_easy_setopt(e, CURLOPT_TIMEOUT, (long)timeout);
				curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT_MS, std::min(kConnectMs, (long)timeout * 1000));
				curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, 1L);
				curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, 2L);
				curl_easy_setopt(e, CURLOPT_ACCEPT_ENCODING, "");
				curl_easy_setopt(e, CURLOPT_TCP_KEEPALIVE, 1L);
				curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, &onWrite);
				curl_easy_setopt(e, CURLOPT_WRITEDATA, &sink);
				curl_easy_setopt(e, CURLOPT_HEADERFUNCTION, &onHeader);
				curl_easy_setopt(e, CURLOPT_HEADERDATA, &sink);
				curl_easy_setopt(e, CURLOPT_XFERINFOFUNCTION, &onProgress);
				curl_easy_setopt(e, CURLOPT_XFERINFODATA, cancel);
				curl_easy_setopt(e, CURLOPT_NOPROGRESS, 0L);
				curl_easy_setopt(e, CURLOPT_ERRORBUFFER, errbuf);
				if (cookies)
				{
					curl_easy_setopt(e, CURLOPT_SHARE, cookies);
					curl_easy_setopt(e, CURLOPT_COOKIEFILE, "");	// the in-memory engine; the share keeps it per source
				}
				// The design's User-Agent is the player's version, unless the source brings its own: its headers
				// reach this list already (the host merges meta.headers with the call's), so only the default is
				// set here.
				bool ownAgent = false;
				for (const auto& h : headers)
					if (iequals(h.first, "User-Agent")) ownAgent = true;
				if (!ownAgent) curl_easy_setopt(e, CURLOPT_USERAGENT, userAgent.c_str());
				if (!easy.addHeaders(headers)) throw std::runtime_error("out of memory");
				if (easy.list) curl_easy_setopt(e, CURLOPT_HTTPHEADER, easy.list);
				// The name curl is given resolves to this one checked address and nowhere else: a second lookup
				// (DNS rebinding) never happens. An address literal needs no pin, curl dials it directly.
				if (literalAddress(hop.host).empty())
				{
					std::string pin = hop.host + ":" + hop.port + ":" +
						(hop.address.find(':') == std::string::npos ? hop.address : "[" + hop.address + "]");
					easy.resolve = curl_slist_append(easy.resolve, pin.c_str());
					if (!easy.resolve) throw std::runtime_error("out of memory");
					curl_easy_setopt(e, CURLOPT_RESOLVE, easy.resolve);
				}
				if (currentMethod == "POST")
				{
					curl_easy_setopt(e, CURLOPT_POST, 1L);
					curl_easy_setopt(e, CURLOPT_POSTFIELDS, currentBody.data());
					curl_easy_setopt(e, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)currentBody.size());
				}
				else
				{
					curl_easy_setopt(e, CURLOPT_HTTPGET, 1L);
				}

				CURLcode rc = curl_easy_perform(e);
				if (rc != CURLE_OK)
				{
					if (cancelled() || rc == CURLE_ABORTED_BY_CALLBACK) throw std::runtime_error("the source was cancelled");
					if (sink.over) throw std::runtime_error("the response is larger than 8 MB");
					throw std::runtime_error(errbuf[0] ? errbuf : curl_easy_strerror(rc));
				}
				// The transfer may have finished just as the job was cancelled: the rest of the call must not run.
				if (cancelled()) throw std::runtime_error("the source was cancelled");

				long status = 0;
				if (curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK) status = 0;
				char* next = nullptr;
				bool isRedirect = status >= 300 && status < 400 && status != 304 &&
					curl_easy_getinfo(e, CURLINFO_REDIRECT_URL, &next) == CURLE_OK && next && *next;
				if (isRedirect)
				{
					if (++redirects > kMaxRedirects) throw std::runtime_error("too many redirects");
					// What curl's own following did: 301/302/303 turn a POST into a GET, 307/308 keep it.
					if (status == 301 || status == 302 || status == 303)
					{
						currentMethod = "GET";
						currentBody.clear();
					}
					currentUrl = next;
					continue;
				}

				// A site's browser check is not an answer the script can use: end the call with the sentence the user
				// needs, instead of handing back a 403/503 that reads as a vague failure (the design's backlog).
				HttpHeaders answerHeaders(sink.headers.begin(), sink.headers.end());
				BrowserCheck check = detectBrowserCheck((int)status, answerHeaders,
					std::string_view(sink.body).substr(0, 4096));
				if (check != BrowserCheck::None) throw std::runtime_error(browserCheckMessage(check));

				HttpResult r;
				r.status = (int)status;
				char* effective = nullptr;
				if (curl_easy_getinfo(e, CURLINFO_EFFECTIVE_URL, &effective) == CURLE_OK && effective) hop.url = effective;
				r.url = hop.url;
				r.body.swap(sink.body);
				r.headers.swap(sink.headers);
				return r;
			}
		}
	}

	struct MangaWorker::Impl
	{
		struct Pending
		{
			uint64_t serial = 0;
			Job job;
		};

		std::mutex m;
		std::condition_variable cv;
		std::deque<Pending> queue;
		std::vector<Result> results;
		std::map<std::string, CURLSH*> cookies;	// per source, for the worker's lifetime; the thread only
		std::string stateDir, prefsDir, userAgent;
		Uint32 wakeEvent = 0;
		uint64_t nextSerial = 1, runningSerial = 0;
		double idleSeconds = kDefaultIdle;
		std::atomic<bool> secureDns{false};	// the Manga setting; the thread reads it at each hop
		DohClient doh;				// Secure DNS's answers and cache, on the thread only
		std::atomic<bool> abortCurrent{false};
		std::atomic<int> openCount{0};
		std::atomic<NetBackend*> netBusy{nullptr};	// the backend in a network folder call now, for cancel()
		std::thread thread;
		bool started = false, stopping = false, eventPending = false, dropCurrent = false;

		~Impl()
		{
			for (auto& kv : cookies)
				if (kv.second) curl_share_cleanup(kv.second);
		}

		void run();
		Result runJob(const Job& job, std::map<std::string, std::shared_ptr<SourceState>>& open);
		Result runNetJob(const Job& job);
		std::shared_ptr<SourceState> openSource(const Job& job,
			std::map<std::string, std::shared_ptr<SourceState>>& open, std::string& error);
		CURLSH* jarFor(const std::string& fullId);
		void closeIdle(std::map<std::string, std::shared_ptr<SourceState>>& open, Clock::time_point now);
		void pushEvent();
	};

	CURLSH* MangaWorker::Impl::jarFor(const std::string& fullId)
	{
		auto it = cookies.find(fullId);
		if (it != cookies.end()) return it->second;
		CURLSH* jar = curl_share_init();
		if (!jar) return nullptr;
		curl_share_setopt(jar, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
		cookies[fullId] = jar;
		return jar;
	}

	// The source's state is opened on first use, not when the reader starts: most sources are never touched, and a
	// state is 32 MB at most (5.4).
	std::shared_ptr<SourceState> MangaWorker::Impl::openSource(const Job& job,
		std::map<std::string, std::shared_ptr<SourceState>>& open, std::string& error)
	{
		if (job.sourceId.empty())
		{
			error = "no source";
			return nullptr;
		}
		std::string path = job.scriptPath;
		if (path.empty() && !stateDir.empty())
		{
			path = stateDir + "/manga/sources/" + repoHashOf(job.sourceId) + "/" + sourceIdOf(job.sourceId) + ".lua";
		}
		if (path.empty())
		{
			error = "no source script";
			return nullptr;
		}
		auto it = open.find(job.sourceId);
		if (it != open.end())
		{
			// An update changed the file: the old state ran the old script, so it can't answer for the new one.
			if (it->second->scriptPath == path) return it->second;
			open.erase(it);
			openCount--;
		}
		std::string text;
		if (!readWhole(path, text))
		{
			error = "can't read " + path;
			return nullptr;
		}
		std::shared_ptr<SourceState> st(new SourceState);
		st->fullId = job.sourceId;
		st->id = sourceIdOf(job.sourceId);
		st->scriptPath = path;
		st->prefsPath = prefsDir.empty() ? "" : prefsDir + "/" + prefsNameOf(job.sourceId);
		st->userAgent = userAgent;
		st->cookies = jarFor(job.sourceId);
		st->cancel = &abortCurrent;
		st->doh = &doh;
		st->secureDns = &secureDns;
		st->host.reset(new SourceHost());
		// The host owns the lambda and the state owns the host: a raw pointer is the lifetime, not a cycle of owners.
		SourceState* raw = st.get();
		st->host->setHttp([raw](const std::string& method, const std::string& url,
							 const std::map<std::string, std::string>& headers, const std::string& body,
							 int timeoutSeconds) {
			return raw->fetch(method, url, headers, body, timeoutSeconds);
		});
		st->host->setPrefsPath(st->prefsPath);
		st->host->setLimits(kCallSeconds, kCallInstructions, kCallMemory);
		st->requestsThisCall = 0;	// the script's top level gets its own 40 (5.4)
		if (!st->host->load(text, st->id, error))
		{
			st->host.reset();
			return nullptr;
		}
		st->lastUse = Clock::now();
		open[job.sourceId] = st;
		openCount++;
		return st;
	}

	MangaWorker::Result MangaWorker::Impl::runNetJob(const Job& job)
	{
		Result out;
		out.kind = job.kind;
		out.sourceId = job.sourceId;
		const char* what = job.kind == Job::NetList ? "list" : job.kind == Job::NetScan ? "scan" : "fetch";
		// (the path is a bpnet:// one: the server's id, never its address or a credential)
		fprintf(stderr, "manga: network folder %s %s\n", what, job.netPath.c_str());
		Uint32 t0 = SDL_GetTicks();
		std::unique_ptr<NetBackend> backend = makeBackend(job.netServer, job.netPassword);
		if (!backend)
		{
			out.error = "No network in this build";
			return out;
		}
		struct Done
		{
			const char* what;
			const Result& r;
			Uint32 t0;
			~Done() { fprintf(stderr, "manga: network folder %s %s in %u ms%s%s\n", what, r.ok ? "done" : "failed",
				SDL_GetTicks() - t0, r.ok ? "" : ": ", r.ok ? "" : r.error.c_str()); }
		} done{what, out, t0};
		NetBusyGuard busy(netBusy, m, backend.get());
		NetPath np;
		if (!parseNetPath(job.netPath, np))
		{
			out.error = "Not a network folder";
			return out;
		}
		auto failed = [&](const NetResult& r) {
			out.ok = false;
			out.error = r.error == NetError::Cancelled && abortCurrent.load() ? "cancelled"
				: netErrorText(r.error, job.netServer.host);
		};
		if (job.kind == Job::NetList || job.kind == Job::NetScan)
		{
			std::vector<NetEntry> entries;
			NetResult r = backend->list(np.path, entries);
			if (!r.ok())
			{
				failed(r);
				return out;
			}
			out.ok = true;
			out.netEntries = entries;
			if (job.kind == Job::NetList) return out;
			// The child folders, one level: a child with pictures is a chapter; one without is not read further
			// (the design's series folder is direct, so a folder of series stays a folder to browse).
			NetMangaListing own = netMangaListing(entries);
			std::map<std::string, std::vector<NetEntry>> sub;
			size_t wanted = 0;
			for (const std::string& d : own.dirs)
			{
				if (abortCurrent.load()) break;
				if (++wanted > 2000) break;	// a readable list, not a whole share
				std::string path = netJoin(job.netPath, d);
				NetPath child;
				if (!parseNetPath(path, child)) continue;
				std::vector<NetEntry> listed;
				NetResult cr = backend->list(child.path, listed);
				if (cr.ok()) sub[path] = std::move(listed);
			}
			std::string title = netBaseName(job.netPath);
			out.netSeries = netMangaSeriesFrom(job.netPath, title.empty() ? "Network folder" : title, entries, sub);
			return out;
		}
		// NetFetch: a chapter into a local folder (its pictures) or a file (an archive)
		if (job.netDest.empty())
		{
			out.error = "No place for the chapter";
			return out;
		}
		std::string parent = job.netDest;
		size_t slash = parent.rfind('/');
		if (slash != std::string::npos && slash > 0) parent = parent.substr(0, slash);
		if (!mkdirP(parent))
		{
			out.error = "Can't write on the card";
			return out;
		}
		if (job.netArchive)
		{
			std::string temp = job.netDest + ".part";
			NetResult openResult;
			std::unique_ptr<NetFile> file = backend->open(np.path, openResult);
			if (!file)
			{
				failed(openResult);
				return out;
			}
			NetCopy copy = copyNetFile(*file, temp, abortCurrent);
			if (!copy.ok)
			{
				unlink(temp.c_str());
				out.error = copy.error;
				return out;
			}
			if (rename(temp.c_str(), job.netDest.c_str()) != 0)
			{
				unlink(temp.c_str());
				out.error = "Can't keep the chapter";
				return out;
			}
			out.ok = true;
			out.netFile = job.netDest;
			return out;
		}
		// a folder chapter: its pictures, in the reading order the listing gives
		std::vector<NetEntry> entries;
		NetResult r = backend->list(np.path, entries);
		if (!r.ok())
		{
			failed(r);
			return out;
		}
		NetMangaListing listing = netMangaListing(entries);
		if (listing.images.empty())
		{
			out.error = "This chapter has no pages";
			return out;
		}
		std::string temp = job.netDest + ".part";
		removeTree(temp);
		if (!mkdirP(temp))
		{
			out.error = "Can't write on the card";
			return out;
		}
		for (const std::string& name : listing.images)
		{
			if (abortCurrent.load())
			{
				out.error = "cancelled";
				break;
			}
			if (!safeName(name))
			{
				out.error = "A page has a name we can't keep";
				break;
			}
			std::string pagePath = job.netPath;
			NetPath page;
			if (!parseNetPath(netJoin(pagePath, name), page))
			{
				out.error = "A page has a name we can't keep";
				break;
			}
			NetResult openResult;
			std::unique_ptr<NetFile> file = backend->open(page.path, openResult);
			if (!file)
			{
				failed(openResult);
				break;
			}
			NetCopy copy = copyNetFile(*file, temp + "/" + name, abortCurrent);
			if (!copy.ok)
			{
				out.error = copy.error;
				break;
			}
		}
		if (!out.error.empty())
		{
			removeTree(temp);
			return out;
		}
		removeTree(job.netDest);
		if (rename(temp.c_str(), job.netDest.c_str()) != 0)
		{
			removeTree(temp);
			out.error = "Can't keep the chapter";
			return out;
		}
		out.ok = true;
		out.netFile = job.netDest;
		return out;
	}

	MangaWorker::Result MangaWorker::Impl::runJob(const Job& job,
		std::map<std::string, std::shared_ptr<SourceState>>& open)
	{
		Result out;
		out.kind = job.kind;
		out.sourceId = job.sourceId;
		if (job.kind == Job::NetList || job.kind == Job::NetScan || job.kind == Job::NetFetch) return runNetJob(job);
		try
		{
			std::shared_ptr<SourceState> st = openSource(job, open, out.error);
			if (!st) return out;
			st->requestsThisCall = 0;	// the 40 per call count from this call's first request (5.4)
			SourceHost& host = *st->host;
			switch (job.kind)
			{
			case Job::Popular: out.ok = host.popular(job.page, out.list, out.error); break;
			case Job::Latest: out.ok = host.latest(job.page, out.list, out.error); break;
			case Job::Search: out.ok = host.search(job.query, job.page, job.json, out.list, out.error); break;
			case Job::Filters: out.ok = host.filters(out.filtersJson, out.error); break;
			case Job::Details: out.ok = host.details(job.json, out.details, out.error); break;
			case Job::Chapters: out.ok = host.chapters(job.json, out.chapters, out.error); break;
			case Job::Pages: out.ok = host.pages(job.json, out.pages, out.error); break;
			case Job::Image:
			{
				MangaImage image;
				out.ok = host.image(job.json, image, out.error);
				if (out.ok)
				{
					out.imageUrl = image.url;
					out.imageHeaders.assign(image.headers.begin(), image.headers.end());
				}
				break;
			}
			case Job::NetList: case Job::NetScan: case Job::NetFetch:
				break;	// (runJob hands these to runNetJob before a source is opened)
			}
			// A call happened, so the 5 minutes count from here even when it failed: the state itself is still good.
			st->lastUse = Clock::now();
			// the source's own headers for the Ui's image fetches (covers): the same for every call to this source,
			// so the Ui keeps them after the first list and uses them for every cover it asks for
			for (const auto& h : host.meta().headers)
				out.sourceHeaders.emplace_back(h.first, h.second);
		}
		catch (const std::exception& e)
		{
			out.ok = false;
			out.error = e.what();
		}
		catch (...)
		{
			out.ok = false;
			out.error = "the source failed";
		}
		return out;
	}

	void MangaWorker::Impl::closeIdle(std::map<std::string, std::shared_ptr<SourceState>>& open, Clock::time_point now)
	{
		auto idle = std::chrono::milliseconds((int64_t)(idleSeconds * 1000.0));
		for (auto it = open.begin(); it != open.end();)
		{
			if (now - it->second->lastUse >= idle)
			{
				it = open.erase(it);
				openCount--;
			}
			else
			{
				++it;
			}
		}
	}

	void MangaWorker::Impl::pushEvent()
	{
		if (!wakeEvent) return;
		SDL_Event ev{};
		ev.type = wakeEvent;
		// A full queue means the loop is behind anyway; not losing the next wake matters more than this one.
		if (SDL_PushEvent(&ev) < 0)
		{
			std::lock_guard<std::mutex> g(m);
			eventPending = false;
		}
	}

	void MangaWorker::Impl::run()
	{
		std::map<std::string, std::shared_ptr<SourceState>> open;
		std::unique_lock<std::mutex> lk(m);
		while (!stopping)
		{
			// A steady queue must not keep an unused source's state (and its 32 MB) alive: the check is cheap.
			closeIdle(open, Clock::now());
			if (queue.empty())
			{
				auto deadline = Clock::time_point::max();
				auto idle = std::chrono::milliseconds((int64_t)(idleSeconds * 1000.0));
				for (const auto& kv : open)
				{
					auto due = kv.second->lastUse + idle;
					if (due < deadline) deadline = due;
				}
				if (deadline == Clock::time_point::max()) cv.wait(lk);
				else cv.wait_until(lk, deadline);
				continue;
			}
			Pending p = std::move(queue.front());
			queue.pop_front();
			runningSerial = p.serial;
			dropCurrent = false;
			abortCurrent = false;
			lk.unlock();
			Result r = runJob(p.job, open);
			r.serial = p.serial;
			lk.lock();
			runningSerial = 0;
			bool drop = dropCurrent || stopping;
			dropCurrent = false;
			bool signal = false;
			if (!drop)
			{
				results.push_back(std::move(r));
				if (!eventPending)
				{
					eventPending = true;
					signal = true;
				}
			}
			lk.unlock();
			if (signal) pushEvent();
			lk.lock();
		}
		lk.unlock();
		// The thread is leaving: the states and their Lua heaps go with it. A stop never waits for this.
		open.clear();
		openCount = 0;
	}

	MangaWorker::MangaWorker() : impl(new Impl)
	{
		// libcurl sets itself up once, before its first use on a thread (as MangaFetch and net_ftp).
		static const bool once = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
		(void)once;
#ifdef BP_VERSION
		impl->userAgent = std::string("BananaPlayer/") + BP_VERSION;
#else
		impl->userAgent = "BananaPlayer";
#endif
	}

	// The owner never destroys it (the thread outlives the player's loop); this only keeps a misuse from leaving the
	// thread running against an object that is going away.
	MangaWorker::~MangaWorker()
	{
		stop();
	}

	void MangaWorker::start(Uint32 wakeEvent, const std::string& stateDir)
	{
		std::shared_ptr<Impl> d = impl;
		std::string prefsDir;
		{
			std::lock_guard<std::mutex> g(d->m);
			if (d->started || d->stopping) return;
			d->started = true;
			d->wakeEvent = wakeEvent;
			d->stateDir = stateDir;
			d->prefsDir = stateDir.empty() ? "" : stateDir + "/manga/prefs";
			prefsDir = d->prefsDir;
		}
		// Before the thread: bp.prefs writes beside the file and a missing folder would fail every set().
		if (!prefsDir.empty()) mkdirP(prefsDir);
		d->thread = std::thread([d] { d->run(); });
		d->thread.detach();
	}

	uint64_t MangaWorker::submit(const Job& job)
	{
		std::shared_ptr<Impl> d = impl;
		std::lock_guard<std::mutex> g(d->m);
		if (d->stopping || !d->started) return 0;
		uint64_t serial = d->nextSerial++;
		Impl::Pending p;
		p.serial = serial;
		p.job = job;
		// A newer job for the same source and kind replaces the waiting one where it stood, so a fast scroll doesn't
		// queue ten Popular pages and a slow source doesn't lose its turn to them. A job that says replace=false (the
		// web dashboard's chapter list, which may be asked beside the handheld's page) is never replaced: it waits.
		for (auto it = d->queue.begin(); it != d->queue.end(); ++it)
		{
			if (job.replace && it->job.replace && it->job.sourceId == job.sourceId && it->job.kind == job.kind)
			{
				*it = std::move(p);
				d->cv.notify_one();
				return serial;
			}
		}
		d->queue.push_back(std::move(p));
		d->cv.notify_one();
		return serial;
	}

	void MangaWorker::cancel(uint64_t serial)
	{
		std::shared_ptr<Impl> d = impl;
		std::lock_guard<std::mutex> g(d->m);
		for (auto it = d->queue.begin(); it != d->queue.end(); ++it)
		{
			if (it->serial == serial)
			{
				d->queue.erase(it);
				return;
			}
		}
		for (auto it = d->results.begin(); it != d->results.end(); ++it)
		{
			if (it->serial == serial)
			{
				d->results.erase(it);
				return;
			}
		}
		// Running now: the progress callback makes its request fail at once, and the answer is dropped (the host has
		// no cancel hook of its own; a pure Lua loop is bounded by the call's time budget).
		if (d->runningSerial == serial)
		{
			d->abortCurrent = true;
			d->dropCurrent = true;
			// a network folder call waits on a server, not on curl: the backend's own cancel is the one that ends it
			if (NetBackend* backend = d->netBusy.load()) backend->cancel();
			d->cv.notify_one();
		}
	}

	void MangaWorker::cancelAll()
	{
		std::shared_ptr<Impl> d = impl;
		std::lock_guard<std::mutex> g(d->m);
		d->queue.clear();
		d->results.clear();
		d->dropCurrent = true;
		d->abortCurrent = true;
		if (NetBackend* backend = d->netBusy.load()) backend->cancel();
		d->cv.notify_one();
	}

	std::vector<MangaWorker::Result> MangaWorker::take()
	{
		std::shared_ptr<Impl> d = impl;
		std::lock_guard<std::mutex> g(d->m);
		d->eventPending = false;
		std::vector<Result> out;
		out.swap(d->results);
		return out;
	}

	void MangaWorker::stop()
	{
		std::shared_ptr<Impl> d = impl;
		std::lock_guard<std::mutex> g(d->m);
		d->stopping = true;
		d->queue.clear();
		d->abortCurrent = true;
		d->dropCurrent = true;
		if (NetBackend* backend = d->netBusy.load()) backend->cancel();
		d->cv.notify_all();
	}

	bool MangaWorker::validateScript(const std::string& text, const std::string& expectedId, std::string& error)
	{
		error.clear();
		SourceHost host;
		// A script is checked before it is installed; the network at that moment is neither expected nor wanted, and
		// a top-level request would otherwise pass a page's HTML back into the check.
		host.setHttp([](const std::string&, const std::string&, const std::map<std::string, std::string>&,
					 const std::string&, int) -> HttpResult {
			throw std::runtime_error("a source script can't use the network while it is checked");
		});
		host.setLimits(kCheckSeconds, kCheckInstructions, kCallMemory);
		return host.load(text, expectedId, error);
	}

	void MangaWorker::setSecureDns(bool on)
	{
		std::shared_ptr<Impl> d = impl;
		std::lock_guard<std::mutex> g(d->m);
		d->secureDns = on;
	}

	void MangaWorker::setIdleSeconds(double seconds)
	{
		std::shared_ptr<Impl> d = impl;
		std::lock_guard<std::mutex> g(d->m);
		d->idleSeconds = seconds > 0 ? seconds : 0;
		d->cv.notify_one();	// a shorter idle must be seen by a thread already waiting
	}

	int MangaWorker::openSourceCount() const
	{
		return impl->openCount.load();
	}
}
