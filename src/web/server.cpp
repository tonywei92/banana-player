// The web dashboard's server: lifecycle, the gate, the PIN sign-in and sessions, the static app, live updates (SSE)
// and the bridge to the main thread. See server.h.
#include "web/server.h"
#include "web/assets.h"
#include "web/subs.h"
#include "json.h"
#include "webauth.h"
#include "webpath.h"
#include <SDL.h>
#include <httplib.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace bp
{
	namespace
	{
		const int MAX_STREAMS = 4;
		const size_t RECENT_EVENTS = 32;
		const char* COOKIE = "bp_s";

		thread_local WebCall* currentCall = nullptr;

		int64_t steadyMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		int64_t wallS()
		{
			return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		}

		// the server's threads give way to the player: a busy browser never makes a song stutter or a frame drop
		void lowerThisThread()
		{
#ifdef __linux__
			static thread_local bool lowered = false;
			if (lowered) return;
			lowered = true;
			// on Linux a thread's own nice value (the tid as the "process")
			setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);
			// best effort, the lowest level: a big upload doesn't hold up mpv reading the card
			const int IOPRIO_WHO_PROCESS = 1, IOPRIO_CLASS_BE = 2, IOPRIO_CLASS_SHIFT = 13;
			syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, 0, (IOPRIO_CLASS_BE << IOPRIO_CLASS_SHIFT) | 7);
#endif
		}

		// httplib's ThreadPool (one thread kept, up to 12 (the live streams hold one each), 16 waiting, extra threads gone after 3 s idle), each of its
		// threads lowered as it takes its first connection (ThreadPool is final: wrapped, not derived)
		class LowPool : public httplib::TaskQueue
		{
		public:
			LowPool() : pool(1, 12, 16, 3) {}
			bool enqueue(std::function<void()> fn) override
			{
				return pool.enqueue([fn = std::move(fn)] {
					lowerThisThread();
					fn();
				});
			}
			void shutdown() override { pool.shutdown(); }

		private:
			httplib::ThreadPool pool;
		};

		bool readFile(const std::string& path, std::string& out)
		{
			std::ifstream f(path, std::ios::binary);
			if (!f) return false;
			std::ostringstream ss;
			ss << f.rdbuf();
			out = ss.str();
			return true;
		}

		// whole or not at all, never readable by others: the PIN and the session hashes
		bool writePrivate(const std::string& path, const std::string& text)
		{
			std::string tmp = path + ".tmp";
			FILE* f = fopen(tmp.c_str(), "wb");
			if (!f) return false;
			chmod(tmp.c_str(), 0600);
			bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
			ok = fclose(f) == 0 && ok;
			if (!ok || rename(tmp.c_str(), path.c_str()) != 0)
			{
				remove(tmp.c_str());
				return false;
			}
			return true;
		}

		std::string lower(std::string s)
		{
			for (char& c : s) c = (char)tolower((unsigned char)c);
			return s;
		}

		// BP_WEB_DIR: the app's files from disk while it's being written (a reload shows an edit)
		const char* mimeByExt(const std::string& path)
		{
			std::string ext = lower(path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.')));
			if (ext == ".html") return "text/html; charset=utf-8";
			if (ext == ".js") return "text/javascript; charset=utf-8";
			if (ext == ".css") return "text/css; charset=utf-8";
			if (ext == ".svg") return "image/svg+xml";
			if (ext == ".ttf") return "font/ttf";
			if (ext == ".woff2") return "font/woff2";
			if (ext == ".txt") return "text/plain; charset=utf-8";
			if (ext == ".png") return "image/png";
			if (ext == ".json") return "application/json";
			if (ext == ".wasm") return "application/wasm";
			return "application/octet-stream";
		}

		std::string fnvHex(const std::string& s)
		{
			uint64_t h = 1469598103934665603ull;
			for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
			char buf[17];
			snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
			return buf;
		}

		std::string sessionCookie(const std::string& token, int64_t maxAge)
		{
			return std::string(COOKIE) + "=" + token + "; HttpOnly; SameSite=Strict; Path=/; Max-Age=" + std::to_string(maxAge);
		}

		void securityHeaders(httplib::Response& res)
		{
			res.set_header("X-Content-Type-Options", "nosniff");
			res.set_header("X-Frame-Options", "DENY");
			res.set_header("Referrer-Policy", "no-referrer");
		}
	}

	struct WebServer::Auth
	{
		SessionStore sessions;
		PinGate gate;
	};

	// ---- the bridge's call ----

	void WebCall::run(Player& p, Ui& u)
	{
		{
			std::lock_guard<std::mutex> g(m);
			if (abandoned) return;
			started = true;
		}
		std::string out;
		currentCall = this;
		try
		{
			out = fn ? fn(p, u) : std::string("{}");
		}
		catch (const std::exception& e)
		{
			status = 500;
			out = JsonWriter().obj().key("error").str(e.what()).end().out();
		}
		currentCall = nullptr;
		{
			std::lock_guard<std::mutex> g(m);
			result = std::move(out);
			done = true;
		}
		cv.notify_all();
	}

	void WebCall::setStatus(int s)
	{
		if (currentCall) currentCall->status = s;
	}

	std::shared_ptr<WebCall> WebCall::take(void* data1)
	{
		auto* holder = (std::shared_ptr<WebCall>*)data1;
		if (!holder) return nullptr;
		std::shared_ptr<WebCall> c = *holder;
		delete holder;
		return c;
	}

	// ---- helpers for the routes ----

	std::string WebServer::clientIp(const httplib::Request& req)
	{
		const std::string& a = req.remote_addr;
		if (a.compare(0, 7, "::ffff:") == 0) return a.substr(7);
		return a;
	}

	void WebServer::json(httplib::Response& res, const std::string& body, int status)
	{
		res.status = status;
		res.set_header("Cache-Control", "no-store");
		res.set_content(body, "application/json; charset=utf-8");
	}

	void WebServer::fail(httplib::Response& res, int status, const std::string& message)
	{
		json(res, JsonWriter().obj().key("error").str(message).end().out(), status);
	}

	bool WebServer::resolve(const std::string& vpath, std::string& real, bool forCreate, httplib::Response& res) const
	{
		std::vector<WebRoot> r;
		std::vector<std::string> d;
		{
			std::lock_guard<std::mutex> g(lock);
			r = cfg.roots;
			d = denied;
		}
		if (resolveVirtual(r, d, vpath, real, forCreate)) return true;
		fail(res, 404, "No such file or folder");
		return false;
	}

	std::string WebServer::toVirtual(const std::string& real) const
	{
		std::lock_guard<std::mutex> g(lock);
		return bp::toVirtual(cfg.roots, real);
	}

	std::string WebServer::servedVirtual(const std::string& real) const
	{
		std::lock_guard<std::mutex> g(lock);
		return bp::servedVirtual(cfg.roots, denied, real);
	}

	bool WebServer::denies(const std::string& real) const
	{
		std::vector<std::string> d;
		{
			std::lock_guard<std::mutex> g(lock);
			d = denied;
		}
		return deniedPath(d, real);
	}

	void WebServer::setRoots(const std::vector<WebRoot>& r)
	{
		std::lock_guard<std::mutex> g(lock);
		cfg.roots = r;
	}

	std::vector<WebRoot> WebServer::roots() const
	{
		std::lock_guard<std::mutex> g(lock);
		return cfg.roots;
	}

	std::string WebServer::error() const
	{
		std::lock_guard<std::mutex> g(lock);
		return why;
	}

	std::string WebServer::pin() const
	{
		std::lock_guard<std::mutex> g(lock);
		return curPin;
	}

	// ---- PIN and sessions ----

	void WebServer::savePin()
	{
		if (cfg.stateDir.empty()) return;
		writePrivate(cfg.stateDir + "/web.conf", "pin = " + curPin + "\n");
	}

	void WebServer::saveSessions()
	{
		if (cfg.stateDir.empty()) return;
		writePrivate(cfg.stateDir + "/web-sessions.tsv", auth->sessions.serialize());
	}

	std::string WebServer::newPin()
	{
		std::string p;
		{
			std::lock_guard<std::mutex> g(lock);
			curPin = p = makePin();
			savePin();
			auth->sessions.clear();
			auth->gate.reset();
			saveSessions();
		}
		// every stream says why it ends (the page shows the sign-in with it), and ends
		goodbye("pin");
		return p;
	}

	bool WebServer::sessionValid(const std::string& hash)
	{
		std::lock_guard<std::mutex> g(lock);
		bool renew;
		return !hash.empty() && auth && auth->sessions.check(hash, wallS(), renew);
	}

	bool WebServer::authed(const httplib::Request& req, httplib::Response& res)
	{
		std::string token = cookieValue(req.get_header_value("Cookie"), COOKIE);
		if (!token.empty())
		{
			std::string hash = hashToken(token);
			std::lock_guard<std::mutex> g(lock);
			bool renew = false;
			int64_t now = wallS();
			if (auth->sessions.check(hash, now, renew))
			{
				// a browser used now and then stays signed in: a fresh week past half of the last one
				if (renew)
				{
					auth->sessions.renew(hash, now + WEB_SESSION_S);
					saveSessions();
					res.set_header("Set-Cookie", sessionCookie(token, WEB_SESSION_S));
				}
				return true;
			}
		}
		fail(res, 401, "Signed out");
		return false;
	}

	// ---- the gate: before any route, and before a request's body is read ----

	bool WebServer::gate(const httplib::Request& req, httplib::Response& res)
	{
		securityHeaders(res);
		if (quitting)
		{
			fail(res, 503, "Stopping");
			return true;
		}
		// the LAN only: a port forwarded on the router, or a tunnel, still finds nothing
		if (!privateAddress(clientIp(req)))
		{
			fail(res, 403, "Not from this network");
			return true;
		}
		// DNS rebinding: a page on a name that later points at the handheld is refused by its Host
		if (!hostAllowed(req.get_header_value("Host")))
		{
			fail(res, 403, "Not this host");
			return true;
		}
		bool safe = req.method == "GET" || req.method == "HEAD";
		if (!safe)
		{
			// CSRF: a form or a plain fetch from another site can't add the header, and SameSite=Strict keeps the cookie
			// home; the Origin, when a browser sends one, has to be this very address.
			// One exception: a page's goodbye (/api/bye) goes by sendBeacon as the tab closes, which can't add a
			// header. It must then carry our Origin (a beacon does), and it names the tab and its page load's random
			// id, which another site can't know; all it can do is mark that one tab gone
			bool beacon = req.path == "/api/bye";
			if (beacon && !req.has_header("Origin"))
			{
				fail(res, 403, "Wrong origin");
				return true;
			}
			if (!beacon && req.get_header_value("X-BP") != "1")
			{
				fail(res, 403, "Missing X-BP");
				return true;
			}
			if (req.has_header("Origin") && lower(req.get_header_value("Origin")) != "http://" + lower(req.get_header_value("Host")))
			{
				fail(res, 403, "Wrong origin");
				return true;
			}
		}
		const std::string& path = req.path;
		if (path.compare(0, 5, "/api/") != 0)
		{
			// the app itself: no sign-in (the login page needs it, and none of it is private)
			if (safe) return serveStatic(req, res);
			fail(res, 404, "Not found");
			return true;
		}
		// the login, and the ping a page that lost the handheld sends till it answers (signed in or not, it's here)
		if (path == "/api/login" || path == "/api/ping") return false;
		return !authed(req, res);
	}

	bool WebServer::serveStatic(const httplib::Request& req, httplib::Response& res)
	{
		std::string rel = req.path == "/" ? "index.html" : req.path.substr(1);
		// nothing above the app's folder, nothing hidden
		bool bad = rel.empty() || rel.find('\0') != std::string::npos || rel.find('\\') != std::string::npos;
		for (size_t i = 0; !bad && i < rel.size();)
		{
			size_t j = rel.find('/', i);
			if (j == std::string::npos) j = rel.size();
			if (j == i || rel[i] == '.') bad = true;
			i = j + 1;
		}
		if (bad)
		{
			fail(res, 404, "Not found");
			return true;
		}

		std::string etag;
		const char* mime = nullptr;
		const unsigned char* data = nullptr;
		size_t len = 0;
		std::string disk;
		// from disk while developing; what isn't there (the fonts, which live in assets/) still comes from the build
		if (!cfg.webDir.empty() && readFile(cfg.webDir + "/" + rel, disk))
		{
			etag = fnvHex(disk);
			mime = mimeByExt(rel);
			data = (const unsigned char*)disk.data();
			len = disk.size();
		}
		else
		{
			const WebAsset* a = nullptr;
			for (size_t i = 0; i < WEB_ASSET_COUNT && !a; i++)
				if (rel == WEB_ASSETS[i].path) a = &WEB_ASSETS[i];
			if (!a)
			{
				fail(res, 404, "Not found");
				return true;
			}
			etag = a->etag;
			mime = a->mime;
			data = a->data;
			len = a->len;
		}
		// asked again each time (no-cache), answered with a 304 unless it changed: an update shows at once
		std::string quoted = "\"" + etag + "\"";
		res.set_header("ETag", quoted);
		res.set_header("Cache-Control", "no-cache");
		std::string inm = req.get_header_value("If-None-Match");
		if (!inm.empty() && (inm == quoted || inm == "W/" + quoted || inm == "*"))
		{
			res.status = 304;
			return true;
		}
		res.status = 200;
		res.set_content((const char*)data, len, mime);
		return true;
	}

	// ---- sign in, sign out ----

	void WebServer::login(const httplib::Request& req, httplib::Response& res)
	{
		std::string ip = clientIp(req);
		std::string typed = req.get_param_value("pin");
		std::string token;
		PinResult r;
		{
			std::lock_guard<std::mutex> g(lock);
			r = auth->gate.attempt(ip, pinMatches(typed, curPin), steadyMs());
			if (r.ok)
			{
				token = newToken();
				auth->sessions.add(hashToken(token), wallS() + WEB_SESSION_S);
				saveSessions();
			}
		}
		if (r.ok)
		{
			res.set_header("Set-Cookie", sessionCookie(token, WEB_SESSION_S));
			json(res, "{\"ok\":true}");
			post(WebCall::Kind::ClientConnected);
			return;
		}
		if (r.rotate)
		{
			// someone is guessing: a new PIN the guesser can't know, and the handheld says so
			newPin();
			post(WebCall::Kind::PinRotated);
			fprintf(stderr, "web: too many wrong PINs, a new one\n");
		}
		if (r.retryAfterS > 0) res.set_header("Retry-After", std::to_string(r.retryAfterS));
		json(res, JsonWriter().obj().key("error").str(r.retryAfterS > 0 ? "Too many tries" : "Wrong PIN").key("left").num(r.left).key("retryAfter").num(r.retryAfterS).end().out(), 401);
	}

	void WebServer::logout(const httplib::Request& req, httplib::Response& res)
	{
		std::string token = cookieValue(req.get_header_value("Cookie"), COOKIE);
		{
			std::lock_guard<std::mutex> g(lock);
			auth->sessions.remove(hashToken(token));
			saveSessions();
		}
		{
			std::lock_guard<std::mutex> g(hubLock);
			authEpoch++;
		}
		hubCv.notify_all();
		res.set_header("Set-Cookie", sessionCookie("", 0));
		json(res, "{\"ok\":true}");
	}

	// ---- live updates ----

	void WebServer::publish(const std::string& j)
	{
		{
			std::lock_guard<std::mutex> g(hubLock);
			stateId = ++lastId;
			stateJson = j;
		}
		hubCv.notify_all();
	}

	void WebServer::goodbye(const std::string& reason)
	{
		{
			std::lock_guard<std::mutex> g(hubLock);
			byeReason = reason;
			generation++;
		}
		hubCv.notify_all();
	}

	const std::string& WebServer::bootId()
	{
		static const std::string id = [] {
			std::random_device rd;
			char b[17];
			snprintf(b, sizeof b, "%08x%08x", (unsigned)rd(), (unsigned)(rd() ^ (unsigned)steadyMs()));
			return std::string(b);
		}();
		return id;
	}

	bool WebServer::tabUpLocked(const std::string& tab) const
	{
		std::string end = "|" + tab;
		for (auto& [key, token] : tabStreams)
			if (token && key.size() >= end.size() && key.compare(key.size() - end.size(), end.size(), end) == 0) return true;
		return false;
	}

	bool WebServer::tabSaidBye(const std::string& tab) const
	{
		std::lock_guard<std::mutex> g(hubLock);
		return byeTabs.count(tab) > 0;
	}

	bool WebServer::tabConnected(const std::string& tab) const
	{
		if (tab.empty()) return streams > 0;
		std::lock_guard<std::mutex> g(hubLock);
		return tabUpLocked(tab);
	}

	// a page's own word for itself, as the handheld can draw it: printable ASCII (its fonts), trimmed, short
	static std::string cleanTabName(const std::string& in)
	{
		std::string out;
		for (char c : in)
			if (c >= 0x20 && c < 0x7F && out.size() < 32) out += c;
		while (!out.empty() && out.back() == ' ') out.pop_back();
		size_t a = out.find_first_not_of(' ');
		return a == std::string::npos ? "Browser" : out.substr(a);
	}

	std::vector<WebServer::Tab> WebServer::playTabs() const
	{
		std::lock_guard<std::mutex> g(hubLock);
		std::vector<std::pair<uint64_t, std::string>> up;
		for (auto& [tab, info] : tabInfo)
			if (info.play && tabUpLocked(tab)) up.push_back({info.since, tab});
		std::sort(up.begin(), up.end());
		std::vector<Tab> out;
		std::map<std::string, int> seen;
		for (auto& [since, tab] : up)
		{
			const std::string& name = tabInfo.at(tab).name;
			int n = ++seen[name];
			out.push_back({tab, n > 1 ? name + " " + std::to_string(n) : name});
		}
		return out;
	}

	std::string WebServer::tabName(const std::string& tab) const
	{
		for (auto& t : playTabs())
			if (t.id == tab) return t.name;
		std::lock_guard<std::mutex> g(hubLock);
		auto it = tabInfo.find(tab);
		return it == tabInfo.end() ? "Browser" : it->second.name;
	}

	void WebServer::notify(const std::string& event, const std::string& dataJson)
	{
		{
			std::lock_guard<std::mutex> g(hubLock);
			recent.push_back({++lastId, event, dataJson.empty() ? "{}" : dataJson});
			while (recent.size() > RECENT_EVENTS) recent.pop_front();
		}
		hubCv.notify_all();
	}

	void WebServer::events(const httplib::Request& req, httplib::Response& res)
	{
		// one stream per open tab, and few of them: each holds a server thread. The same tab again (?tab=, kept across
		// its reloads) replaces its old one, which ends at once
		std::string tab = req.get_param_value("tab");
		std::string key = hashToken(cookieValue(req.get_header_value("Cookie"), COOKIE)) + "|" + tab;
		bool replacing = false;
		if (!tab.empty())
		{
			std::lock_guard<std::mutex> g(hubLock);
			auto it = tabStreams.find(key);
			replacing = it != tabStreams.end() && it->second != 0;
		}
		int n = streams.load();
		do
		{
			if (n >= MAX_STREAMS && !replacing)
			{
				res.set_header("Retry-After", "10");
				fail(res, 503, "Too many browsers");
				return;
			}
		} while (!streams.compare_exchange_weak(n, n + 1));
		post(WebCall::Kind::ClientsChanged);

		struct Stream
		{
			uint64_t sent = 0, epoch = 0, token = 0;
			int gen = 0;
			bool first = true;
			std::string session, key;
		};
		auto s = std::make_shared<Stream>();
		s->session = hashToken(cookieValue(req.get_header_value("Cookie"), COOKIE));
		s->key = tab.empty() ? "" : key;
		{
			std::lock_guard<std::mutex> g(hubLock);
			if (!s->key.empty())
			{
				tabStreams[s->key] = s->token = ++streamSeq;
				tabPage[s->key] = req.get_param_value("pg");
				byeTabs.erase(tab);	// (back: a reload, or the page shown again)
				// its name (it keeps its place in the list across reconnects while it's known)
				TabInfo& info = tabInfo[tab];
				if (!info.since) info.since = s->token;
				info.name = cleanTabName(req.get_param_value("name"));
				info.play = req.get_param_value("play") != "0";
				// (a few dozen tabs known at most: the oldest not connected go)
				while (tabInfo.size() > 48)
				{
					auto old = tabInfo.end();
					for (auto it = tabInfo.begin(); it != tabInfo.end(); ++it)
						if (!tabUpLocked(it->first) && (old == tabInfo.end() || it->second.since < old->second.since)) old = it;
					if (old == tabInfo.end()) break;
					tabInfo.erase(old);
				}
			}
			s->gen = generation;
			s->epoch = authEpoch;
			// a reconnect gets what it missed, if it's still here; an id from before a restart is from nothing here
			std::string last = req.get_header_value("Last-Event-ID");
			uint64_t id = last.empty() ? lastId : strtoull(last.c_str(), nullptr, 10);
			s->sent = id <= lastId ? id : lastId;
		}
		if (replacing) hubCv.notify_all();
		res.set_header("Cache-Control", "no-store");
		res.set_header("X-Accel-Buffering", "no");
		int keepS = keepaliveS;
		res.set_chunked_content_provider("text/event-stream",
			[this, s, keepS](size_t, httplib::DataSink& sink) {
				std::string out;
				bool end = false;
				{
					std::unique_lock<std::mutex> g(hubLock);
					auto replaced = [&] { return !s->key.empty() && tabStreams[s->key] != s->token; };
					auto news = [&] { return quitting || generation != s->gen || authEpoch != s->epoch || lastId > s->sent || replaced(); };
					if (!s->first && !hubCv.wait_for(g, std::chrono::seconds(keepS), news))
					{
						g.unlock();
						// an event, not a comment: a page sees it (EventSource hides comments), so a stream that's gone
						// quiet for twice this is one to call dead. It also finds a client gone
						static const char PING[] = "event: ping\ndata: {}\n\n";
						return sink.write(PING, sizeof PING - 1);
					}
					if (replaced()) end = true;	// the same tab has a newer stream: this one's gone quietly
					else if (quitting || generation != s->gen)
					{
						end = true;
						// why, before the end: the page shows that, not "Can't reach" (a stop without a goodbye: none)
						if (generation != s->gen && !byeReason.empty())
							out = "event: bye\ndata: " + JsonWriter().obj().key("reason").str(byeReason).end().out() + "\n\n";
					}
					else
					{
						bool first = s->first;
						if (first) out = "retry: 10000\n\n";
						s->first = false;
						s->epoch = authEpoch;
						// in id order: the missed events, with the state where it came (on connect: always, it's the
						// whole picture)
						bool stateDue = !stateJson.empty() && (first || stateId > s->sent);
						auto addState = [&] {
							out += "id: " + std::to_string(stateId) + "\nevent: state\ndata: " + stateJson + "\n\n";
							stateDue = false;
						};
						for (const Event& e : recent)
						{
							if (e.id <= s->sent) continue;
							// a sleep or a wake from before this stream: past news (a page coming back must not read it
							// as the handheld going to sleep now)
							if (first && (e.name == "bye" || e.name == "awake")) continue;
							if (stateDue && stateId < e.id) addState();
							out += "id: " + std::to_string(e.id) + "\nevent: " + e.name + "\ndata: " + e.data + "\n\n";
						}
						if (stateDue) addState();
						s->sent = lastId;
					}
				}
				// signed out (logout, a new PIN): the stream ends, and the browser's next request finds out why
				if (!end && !sessionValid(s->session)) end = true;
				if (end)
				{
					if (!out.empty()) sink.write(out.data(), out.size());
					sink.done();
					return true;
				}
				return out.empty() || sink.write(out.data(), out.size());
			},
			[this, s](bool) {
				if (!s->key.empty())
				{
					std::lock_guard<std::mutex> g(hubLock);
					auto it = tabStreams.find(s->key);
					// its own entry, or one a goodbye zeroed (a newer stream of the tab has a token of its own: kept)
					if (it != tabStreams.end() && (it->second == s->token || it->second == 0))
					{
						tabStreams.erase(it);
						tabPage.erase(s->key);
					}
				}
				streams--;
				post(WebCall::Kind::ClientsChanged);
			});
	}

	// ---- the bridge ----

	void WebServer::post(WebCall::Kind kind, const std::string& arg)
	{
		if (!cfg.eventType) return;
		auto c = std::make_shared<WebCall>();
		c->kind = kind;
		c->arg = arg;
		SDL_Event e{};
		e.type = cfg.eventType;
		e.user.code = (int)kind;
		e.user.data1 = new std::shared_ptr<WebCall>(c);
		if (SDL_PushEvent(&e) != 1) delete (std::shared_ptr<WebCall>*)e.user.data1;
	}

	std::string WebServer::call(WebFn fn, int timeoutMs, int& status)
	{
		auto busy = [&](const char* why) {
			status = 503;
			return JsonWriter().obj().key("error").str(why).end().out();
		};
		if (quitting || !cfg.eventType) return busy("Stopping");
		auto c = std::make_shared<WebCall>();
		c->fn = std::move(fn);
		{
			std::lock_guard<std::mutex> g(callsLock);
			calls.erase(std::remove_if(calls.begin(), calls.end(), [](const std::weak_ptr<WebCall>& w) { return w.expired(); }), calls.end());
			calls.push_back(c);
		}
		SDL_Event e{};
		e.type = cfg.eventType;
		e.user.code = (int)WebCall::Kind::Call;
		e.user.data1 = new std::shared_ptr<WebCall>(c);
		if (SDL_PushEvent(&e) != 1)
		{
			delete (std::shared_ptr<WebCall>*)e.user.data1;
			return busy("Busy");
		}
		std::unique_lock<std::mutex> g(c->m);
		c->cv.wait_for(g, std::chrono::milliseconds(timeoutMs), [&] { return c->done || quitting; });
		// already running on the main thread: its answer is moments away (a WebFn never blocks)
		if (!c->done && c->started) c->cv.wait_for(g, std::chrono::milliseconds(1000), [&] { return c->done; });
		if (!c->done)
		{
			// the main thread mustn't do it later, when nobody's listening for the answer
			c->abandoned = true;
			return busy(quitting ? "Stopping" : "The player is busy");
		}
		status = c->status;
		return c->result;
	}

	void WebServer::wakeCalls()
	{
		std::lock_guard<std::mutex> g(callsLock);
		for (auto& w : calls)
			if (auto c = w.lock())
			{
				std::lock_guard<std::mutex> cg(c->m);
				c->cv.notify_all();
			}
		calls.clear();
	}

	// ---- lifecycle ----

	void WebServer::routes()
	{
		httplib::Server& s = *svr;
		s.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
			return gate(req, res) ? httplib::Server::HandlerResponse::Handled : httplib::Server::HandlerResponse::Unhandled;
		});
		s.Post("/api/login", [this](const httplib::Request& req, httplib::Response& res) { login(req, res); });
		s.Post("/api/logout", [this](const httplib::Request& req, httplib::Response& res) { logout(req, res); });
		// alive and here: 204, nothing cached, and which run of the player answered (no sign-in needed: the gate lets
		// it through)
		s.Get("/api/ping", [this](const httplib::Request&, httplib::Response& res) {
			res.status = 204;
			res.set_header("Cache-Control", "no-store");
			res.set_header("X-BP-Boot", bootId());
			res.set_header("X-BP-Version", webVersion);
		});
		s.Get("/api/events", [this](const httplib::Request& req, httplib::Response& res) { events(req, res); });
		// a page going away (closed, reloaded, gone to another site) says so: its live stream counts as gone now, not
		// at the next keep-alive that fails (15 s and more), so the Session's grace starts at once and the Play on list
		// drops it. Only the stream of that page load (?pg=): a reload's new stream may already be in when this comes,
		// and stays. A reload comes back within the grace, as before
		s.Post("/api/bye", [this](const httplib::Request& req, httplib::Response& res) {
			std::string tab = req.get_param_value("tab"), pg = req.get_param_value("pg");
			if (!tab.empty() && !pg.empty())
			{
				std::string key = hashToken(cookieValue(req.get_header_value("Cookie"), COOKIE)) + "|" + tab;
				bool ended = false;
				{
					std::lock_guard<std::mutex> g(hubLock);
					auto it = tabStreams.find(key);
					auto pt = tabPage.find(key);
					if (it != tabStreams.end() && it->second && pt != tabPage.end() && pt->second == pg)
					{
						it->second = 0;	// (its stream sees itself replaced and ends; tabUp no longer counts it)
						ended = true;
						if (byeTabs.size() > 48) byeTabs.clear();	// (a few at a time at most; never a growing list)
						byeTabs.insert(tab);
					}
				}
				if (ended)
				{
					hubCv.notify_all();
					fprintf(stderr, "web: a page said goodbye (tab %s)\n", tab.c_str());
				}
			}
			res.status = 204;
		});
		s.Get("/api/state", [this](const httplib::Request&, httplib::Response& res) {
			res.set_header("X-BP-Version", webVersion);
			if (cfg.snapshot)
			{
				int status = 200;
				std::string body = call(cfg.snapshot, 2000, status);
				json(res, body, status);
				return;
			}
			std::lock_guard<std::mutex> g(hubLock);
			json(res, stateJson.empty() ? "{}" : stateJson);
		});
		registerFileRoutes(*this);
		registerPlayerRoutes(*this);
		registerPlaylistRoutes(*this);
		registerMangaRoutes(*this);
		s.set_error_handler([](const httplib::Request&, httplib::Response& res) {
			// a status without a body (a route's own 404, a 405): JSON like every other answer
			if (res.body.empty() && res.status >= 400) fail(res, res.status, httplib::status_message(res.status));
		});
	}

	bool WebServer::start(const WebConfig& c)
	{
		if (st == State::Running) return true;
		{
			std::lock_guard<std::mutex> g(lock);
			cfg = c;
			why.clear();
			if (const char* d = getenv("BP_WEB_DIR")) cfg.webDir = d;
			if (const char* p = getenv("BP_WEB_PORT")) cfg.port = atoi(p);
			if (cfg.port <= 0 || cfg.port > 65535) cfg.port = 8080;
			denied = cfg.deny;
			for (const char* d : {"/data/home", "/data/banana", "/data/store"}) denied.push_back(d);
			if (!cfg.stateDir.empty()) denied.push_back(cfg.stateDir);

			if (!auth) auth = std::make_shared<Auth>();
			auth->gate.reset();
			// the PIN: a test's, the one kept, or a new one kept from now on
			std::string text;
			curPin.clear();
			if (!cfg.stateDir.empty() && readFile(cfg.stateDir + "/web.conf", text))
			{
				std::istringstream in(text);
				std::string line;
				while (std::getline(in, line))
					if (line.compare(0, 3, "pin") == 0)
					{
						size_t eq = line.find('=');
						std::string v = eq == std::string::npos ? "" : line.substr(eq + 1);
						v.erase(0, v.find_first_not_of(" \t"));
						v.erase(v.find_last_not_of(" \t\r") + 1);
						if (validPin(v)) curPin = v;
					}
			}
			if (curPin.empty())
			{
				curPin = makePin();
				savePin();
			}
			if (const char* p = getenv("BP_WEB_PIN"))
				if (validPin(p))
				{
					curPin = p;
					for (char& ch : curPin) ch = (char)toupper((unsigned char)ch);
				}
			auth->sessions.clear();
			if (!cfg.stateDir.empty() && readFile(cfg.stateDir + "/web-sessions.tsv", text)) auth->sessions.parse(text);
		}
		// the subtitles a browser gets (extracted from a file: slow once, then kept) live with the rest of the state
		if (!cfg.stateDir.empty()) setSubsCacheDir(cfg.stateDir + "/webcache");
		keepaliveS = 15;
		// the app's version as this run serves it (BP_WEB_DIR: the files on disk now, the built-in ones where there
		// are none): a page that loaded another, coming back after an update, reloads rather than run old code
		{
			std::string all;
			for (size_t i = 0; i < WEB_ASSET_COUNT; i++)
			{
				std::string disk;
				all += WEB_ASSETS[i].path;
				all += ' ';
				all += !cfg.webDir.empty() && readFile(cfg.webDir + "/" + WEB_ASSETS[i].path, disk) ? fnvHex(disk) : WEB_ASSETS[i].etag;
				all += '\n';
			}
			webVersion = fnvHex(all).substr(0, 12);
		}
		if (const char* k = getenv("BP_WEB_KEEPALIVE"))
			if (atoi(k) > 0) keepaliveS = atoi(k);

		// a new server each time: httplib's can't listen again once stopped. The one before is gone if its thread
		// ended, else left to the process
		svr = new httplib::Server;
		quitting = false;
		{
			std::lock_guard<std::mutex> g(hubLock);
			generation++;
			byeReason.clear();
		}
		svr->new_task_queue = [] { return new LowPool; };
		svr->set_read_timeout(10, 0);
		svr->set_write_timeout(15, 0);
		// short: an idle connection holds a server thread while it waits
		svr->set_keep_alive_timeout(2);
		// httplib's default is SO_REUSEPORT, where a second player would share 8080 with the first instead of moving on
		svr->set_socket_options([](socket_t sock) {
			int on = 1;
			setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof on);
		});
		routes();

		int bound = 0;
		for (int p = cfg.port; p < cfg.port + 10 && p <= 65535 && !bound; p++)
			if (svr->bind_to_port("0.0.0.0", p)) bound = p;
		if (!bound) bound = svr->bind_to_any_port("0.0.0.0");
		if (bound <= 0)
		{
			std::lock_guard<std::mutex> g(lock);
			why = "no port would open";
			st = State::Failed;
			fprintf(stderr, "web: couldn't start: %s\n", why.c_str());
			return false;
		}
		boundPort = bound;
		auto done = std::make_shared<std::pair<std::mutex, std::condition_variable>>();
		auto ended = std::make_shared<bool>(false);
		listenEnded = [done, ended](int timeoutMs) {
			std::unique_lock<std::mutex> g(done->first);
			return done->second.wait_for(g, std::chrono::milliseconds(timeoutMs), [&] { return *ended; });
		};
		httplib::Server* s = svr;
		listener = std::thread([s, done, ended] {
			lowerThisThread();
			s->listen_after_bind();
			{
				std::lock_guard<std::mutex> g(done->first);
				*ended = true;
			}
			done->second.notify_all();
		});
		st = State::Running;
		fprintf(stderr, "web: listening on port %d\n", bound);
		return true;
	}

	void WebServer::stop(const std::string& reason)
	{
		if (st != State::Running) return;
		// the pages told why first, while the streams can still write: a bye each, then their end (300 ms at most:
		// quitting isn't held up by a browser that's slow to take it)
		goodbye(reason);
		for (int64_t until = steadyMs() + 300; streams > 0 && steadyMs() < until;)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		quitting = true;
		wakeCalls();
		{
			std::lock_guard<std::mutex> g(hubLock);
			generation++;
		}
		hubCv.notify_all();
		svr->stop();
		// the pool ends with its last connection: an SSE stream at once, an idle keep-alive within a second. One
		// still writing to a slow client is left to the process
		if (listenEnded(1000))
		{
			listener.join();
			delete svr;
		}
		else
		{
			listener.detach();
			fprintf(stderr, "web: stopped without waiting for the last connection\n");
		}
		svr = nullptr;
		boundPort = 0;
		st = State::Off;
	}
}
