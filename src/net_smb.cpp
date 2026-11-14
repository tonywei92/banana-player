// SMB2/3 through libsmb2: "/" is the server's disk shares (asked of IPC$'s srvsvc pipe), "/Share/a/b" a path in
// one. A share's tree is a libsmb2 context of its own, connected the first time the share is used, and again once
// its connection was lost; an open file gets a context of its own too (mpv reads from another thread than the
// browser lists).
//
// Only libsmb2's asynchronous calls are used, waited on here (wait()): its synchronous wrappers (6.2) free what an
// unanswered request still points at when the connection fails, and the context's end then writes into freed
// memory. A request whose wait ended early (the connection failed, no answer in time, cancelled) is left to its
// callback, which the context's end calls: the context is never served again, only destroyed.
#include "net.h"
#include "netwait.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#include <smb2/libsmb2-dcerpc.h>
#include <smb2/libsmb2-dcerpc-srvsvc.h>

namespace bp
{
	namespace
	{
		using netwait::Clock;
		using netwait::nowMs;
		using netwait::Canceller;
		using netwait::Busy;

		// a request in flight: its answer, or (abandoned) nobody waiting for it any more, which its callback frees
		struct Call
		{
			bool done = false, abandoned = false;
			int status = 0;
			void* data = nullptr;
			void (*drop)(smb2_context*, void*) = nullptr;	// what frees an answer that came too late
		};

		void answered(smb2_context* c, int status, void* data, void* priv)
		{
			Call* k = (Call*)priv;
			if (k->abandoned)
			{
				if (data && k->drop) k->drop(c, data);
				delete k;
				return;
			}
			k->done = true;
			k->status = status;
			k->data = data;
		}

		enum class Wait { Answered, Failed, Timeout, Cancelled };

		// c served until k is answered. The deadline is timeoutMs after the last sign of life (a connection that
		// keeps sending a long listing isn't cut, a stalled one is). Anything but Answered leaves k to its callback
		// and c to be destroyed
		Wait wait(smb2_context* c, Call* k, Canceller& cancel, int timeoutMs)
		{
			int64_t until = nowMs() + timeoutMs;
			while (!k->done)
			{
				if (cancel.cancelled()) { k->abandoned = true; return Wait::Cancelled; }
				int64_t left = until - nowMs();
				if (left <= 0) { k->abandoned = true; return Wait::Timeout; }
				pollfd p[2] = { { smb2_get_fd(c), (short)smb2_which_events(c), 0 }, { cancel.fd(), POLLIN, 0 } };
				// (at most 100 ms: libsmb2's own timeouts are checked in smb2_service, and a cancel can't be missed)
				int n = poll(p, cancel.fd() >= 0 ? 2 : 1, (int)std::min<int64_t>(left, 100));
				if (n < 0 && errno != EINTR) { smb2_set_error(c, "poll failed: %s", strerror(errno)); k->abandoned = true; return Wait::Failed; }
				if (cancel.cancelled()) { k->abandoned = true; return Wait::Cancelled; }
				int revents = n > 0 ? p[0].revents : 0;
				if (revents) until = nowMs() + timeoutMs;
				if (smb2_service(c, revents) < 0 && !k->done) { k->abandoned = true; return Wait::Failed; }
			}
			return Wait::Answered;
		}

		// NTSTATUS codes that mean "the login was refused"
		bool loginRefused(uint32_t st)
		{
			switch (st)
			{
			case 0xC000006D: case 0xC000006E: case 0xC0000070: case 0xC0000071: case 0xC0000072:
			case 0xC0000193: case 0xC0000224: case 0xC0000234:
				return true;
			}
			return false;
		}

		// what else a server's answer may say
		NetError answerError(uint32_t st)
		{
			switch (st)
			{
			case 0xC0000034: case 0xC000003A: case 0xC000000F: case 0xC0000033: case 0xC000003B: case 0xC00000BA:
			case 0xC0000103: case 0xC0000056:
				return NetError::NotFound;
			case 0xC0000022: case 0xC0000043: case 0xC00000CC: case 0xC00000CA:	// (a share that isn't there: NoAccess, as asked)
			case 0xC0000035: case 0xC00000A2: case 0xC0000101:	// a name taken, a write-protected medium, a folder not empty
				return NetError::NoAccess;
			case 0xC00000B5:
				return NetError::Timeout;
			case 0xC00000C9: case 0xC0000203: case 0xC000035C: case 0xC0000205: case 0xC000009A: case 0xC00000C4:
			case 0xC000020C: case 0xC000020D: case 0xC0000236: case 0xC000023C: case 0xC000023D: case 0xC0000128:
			case 0xC0000120: case 0xFFFFFFFF:
				return NetError::Gone;
			}
			return NetError::Other;
		}

		// libsmb2's words for a connection that wasn't made, and the errno in them: "Socket connect failed with 61",
		// "Connect failed with errno : Connection refused(61)"; 0 when it isn't one
		int connectErrno(const std::string& e)
		{
			size_t at = e.find("Socket connect failed with ");
			if (at != std::string::npos) return atoi(e.c_str() + at + 27);
			at = e.find("Connect failed with errno");
			size_t open = at == std::string::npos ? at : e.rfind('(');
			return open != std::string::npos && open > at ? atoi(e.c_str() + open + 1) : 0;
		}

		NetError errnoError(int n)
		{
			switch (n)
			{
			case ECONNREFUSED: case ENOENT: case ENOTDIR: return NetError::NotFound;
			case ENETUNREACH: case EHOSTUNREACH: case ENETDOWN: case EADDRNOTAVAIL: return NetError::NoNetwork;
			case ETIMEDOUT: return NetError::Timeout;
			case EACCES: case EPERM: return NetError::NoAccess;
			case ECONNRESET: case EPIPE: case ENETRESET: case ENOTCONN: case ECONNABORTED: return NetError::Gone;
			}
			return NetError::Other;
		}

		std::string libError(smb2_context* c)
		{
			const char* got = c ? smb2_get_error(c) : nullptr;
			std::string e = got ? got : "";
			while (!e.empty() && strchr("\n .", e.back())) e.pop_back();
			return e;
		}

		NetResult result(NetError err, const std::string& detail)
		{
			NetResult r;
			r.error = err;
			r.detail = detail;
			return r;
		}

		// what a request's end means. how: the wait's end when it wasn't answered; rc: the answer's status
		// (libsmb2's -errno; the server's NTSTATUS, when it answered, is the context's)
		NetResult describe(smb2_context* c, const std::string& what, Wait how, int rc, int timeoutMs)
		{
			std::string e = libError(c);
			switch (how)
			{
			case Wait::Cancelled: return result(NetError::Cancelled, what + ": cancelled");
			case Wait::Timeout: return result(NetError::Timeout, what + ": no answer in " + std::to_string(timeoutMs) + " ms" + (e.empty() ? "" : " (" + e + ")"));
			case Wait::Failed:
			{
				int n = connectErrno(e);
				if (n > 0) return result(errnoError(n) == NetError::Other ? NetError::NotFound : errnoError(n), what + ": " + e);
				return result(NetError::Gone, what + ": the connection was lost" + (e.empty() ? "" : " (" + e + ")"));
			}
			case Wait::Answered: break;
			}
			uint32_t st = (uint32_t)smb2_get_nterror(c);
			// (some of libsmb2's calls give the server's NTSTATUS itself as the status: an error's top bits set)
			if (st == 0 && ((uint32_t)rc & 0xC0000000u) == 0xC0000000u) st = (uint32_t)rc;
			// (and some keep it only in their words, "Create failed with status -1073741790", with an errno beside)
			if ((st & 0xC0000000u) != 0xC0000000u || ((int32_t)st < 0 && (int32_t)st > -4096))
			{
				size_t at = e.find("status -");
				if (at != std::string::npos)
				{
					uint32_t said = (uint32_t)(int32_t)atoll(e.c_str() + at + 7);
					if ((said & 0xC0000000u) == 0xC0000000u) st = said;
				}
			}
			if (st != 0)
			{
				if (loginRefused(st)) return result(NetError::Auth, what + ": " + e);
				return result(answerError(st), what + ": " + (e.empty() ? std::string(nterror_to_str(st)) : e));
			}
			// not the server's answer: libsmb2's own errno (the TCP connect's, among them)
			int n = -rc;
			if (n == ECONNREFUSED && connectErrno(e) == 0) return result(NetError::NotFound, what + ": " + e);
			NetError err = n > 0 && n < 4096 ? errnoError(n) : NetError::Gone;
			if (err == NetError::Other && n > 0 && n < 4096 && e.empty()) e = strerror(n);
			return result(err, what + ": " + e);
		}

		// the server's address as libsmb2 takes it ("[v6]" and ":port" when it isn't 445). The name is resolved
		// here, so a name that isn't one is told apart from a server that doesn't answer (libsmb2 resolves too,
		// but says the same of both)
		NetResult resolve(const NetServer& s, std::string& out)
		{
			addrinfo hints = {};
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			addrinfo* res = nullptr;
			int err = getaddrinfo(s.host.c_str(), nullptr, &hints, &res);
			if (err != 0 || !res)
			{
				if (res) freeaddrinfo(res);
				return result(err == EAI_AGAIN ? NetError::NoNetwork : NetError::NotFound, "Resolving " + s.host + ": " + gai_strerror(err));
			}
			char ip[INET6_ADDRSTRLEN + 1] = {};
			getnameinfo(res->ai_addr, res->ai_addrlen, ip, sizeof ip, nullptr, 0, NI_NUMERICHOST);
			bool v6 = res->ai_family == AF_INET6;
			freeaddrinfo(res);
			out = v6 ? "[" + std::string(ip) + "]" : std::string(ip);
			int port = s.port > 0 ? s.port : defaultPort(NetType::Smb);
			if (port != 445) out += ":" + std::to_string(port);
			return NetResult();
		}

		// the common ground of a backend and its files: the server, and how to reach a share in it
		struct Link
		{
			NetServer server;
			std::string password;
			int timeoutMs;
			// a guest over SMB 3.1.1: libsmb2 6.2 signs the requests after the login with a key the server (Samba)
			// has none of for a guest, and the share is refused. 3.0.2 doesn't sign them; a server without 3.0.2
			// (Windows 8 and older, an old NAS) is asked for SMB 2 instead
			smb2_negotiate_version dialect;

			Link(const NetServer& s, const std::string& pw, int ms)
				: server(s), password(pw), timeoutMs(ms), dialect(s.guest ? SMB2_VERSION_0302 : SMB2_VERSION_ANY) {}

			// a request on c: started (start(k) with answered and k), then waited on. A failure to start, or a wait
			// that ended without the answer, is the result (c is then dead: lost says so); else status and data are
			// the answer's (status < 0: -errno)
			template <class Start> NetResult call(smb2_context* c, Canceller& cancel, const std::string& what, Start start,
				int& status, void*& data, bool& lost, void (*drop)(smb2_context*, void*) = nullptr)
			{
				smb2_set_error(c, "");	// (libsmb2 keeps the last answer's status until another comes)
				Call* k = new Call();
				k->drop = drop;
				int rc = start(k);
				if (rc != 0) { delete k; return describe(c, what, Wait::Answered, rc < 0 ? rc : -EIO, timeoutMs); }
				Wait w = wait(c, k, cancel, timeoutMs);
				if (w != Wait::Answered) { lost = true; return describe(c, what, w, 0, timeoutMs); }
				status = k->status;
				data = k->data;
				delete k;
				return NetResult();
			}

			// a context logged in to the server and connected to the share; nullptr and r why not
			smb2_context* open(const std::string& share, Canceller& cancel, NetResult& r)
			{
				std::string addr;
				r = resolve(server, addr);
				if (!r.ok()) return nullptr;
				smb2_context* c = connect(share, addr, dialect, cancel, r);
				if (!c && dialect == SMB2_VERSION_0302 && r.error != NetError::Auth && r.error != NetError::NotFound &&
					r.error != NetError::NoNetwork && r.error != NetError::Timeout && r.error != NetError::Cancelled)
				{
					c = connect(share, addr, SMB2_VERSION_ANY2, cancel, r);
					if (c) dialect = SMB2_VERSION_ANY2;	// (the server's other shares the same)
				}
				return c;
			}

			smb2_context* connect(const std::string& share, const std::string& addr, smb2_negotiate_version version, Canceller& cancel, NetResult& r)
			{
				smb2_context* c = smb2_init_context();
				if (!c) { r = result(NetError::Other, "No memory for an SMB context"); return nullptr; }
				smb2_set_version(c, version);
				smb2_set_security_mode(c, SMB2_NEGOTIATE_SIGNING_ENABLED);
				// (libsmb2's own timeout, in whole seconds, ends a request the server never answers while the
				// connection seems alive; the wait's deadline is the finer one)
				smb2_set_timeout(c, std::max(1, (timeoutMs + 999) / 1000));
				// guest: Samba maps an unknown user to its guest account ("map to guest = bad user"); Windows and
				// macOS take Guest with no password
				std::string user = server.guest ? "guest" : server.user, pass = server.guest ? "" : password;
				smb2_set_user(c, user.c_str());
				smb2_set_password(c, pass.c_str());
				smb2_set_workstation(c, "BANANA");
				int status = 0;
				void* data = nullptr;
				bool lost = false;
				std::string what = share == "IPC$" ? "Connecting to " + server.host : "Opening the share " + share;
				r = call(c, cancel, what, [&](Call* k) { return smb2_connect_share_async(c, addr.c_str(), share.c_str(), user.c_str(), answered, k); },
					status, data, lost);
				if (r.ok() && status != 0) r = describe(c, what, Wait::Answered, status, timeoutMs);
				// a connection that ended before it was made: refused (macOS shows a refused connect as POLLHUP, which
				// libsmb2 reports as a lost connection, not as ECONNREFUSED), so nothing listens there
				if (r.error == NetError::Gone) r.error = NetError::NotFound;
				if (r.ok()) return c;
				smb2_destroy_context(c);
				return nullptr;
			}
		};

		// "/share/a/b" -> "share", "a/b"
		void split(const std::string& p, std::string& share, std::string& sub)
		{
			size_t from = p.find_first_not_of('/');
			std::string c = from == std::string::npos ? "" : p.substr(from);
			while (!c.empty() && c.back() == '/') c.pop_back();
			size_t slash = c.find('/');
			share = c.substr(0, slash);
			sub = slash == std::string::npos ? "" : c.substr(slash + 1);
		}

		// a server's answer that a new connection gets past (the session or the share let go, the link gone)
		bool transient(NetError e) { return e == NetError::Gone || e == NetError::Timeout; }

		class SmbFile : public NetFile
		{
		public:
			SmbFile(const Link& link, const std::string& share, const std::string& sub) : link(link), share(share), sub(sub) {}

			~SmbFile() override { release(); }

			// the first open, on the backend's thread and under its cancel (the file's own takes over after)
			NetResult start(Canceller& backend)
			{
				waiter = &backend;
				NetResult r = reopen(true);
				waiter = &own;
				last = r;
				return r;
			}

			int64_t read(char* buf, int64_t n, int64_t offset) override
			{
				Busy b(own);
				if (n <= 0) return 0;
				int64_t got = 0;
				int64_t dropped = 0;	// when the connection was lost (its tries end 20 s after)
				int backoff = 1000;
				while (got < n)
				{
					if (!ctx)
					{
						// gone: another connection, with time for the server to come back (a NAS that restarts)
						if (!dropped) dropped = nowMs();
						else if (nowMs() - dropped >= 20000) { last = result(NetError::Gone, "Reading " + sub + ": the connection was lost and not got back"); return got ? got : -1; }
						else if (!waiter->sleep(backoff)) { last = result(NetError::Cancelled, "Reading " + sub + ": cancelled"); return -1; }
						NetResult r = reopen(false);
						if (r.error == NetError::Cancelled) { last = r; return -1; }
						if (!r.ok() && !transient(r.error) && r.error != NetError::NotFound) { last = r; return got ? got : -1; }
						if (r.ok()) { dropped = 0; backoff = 1000; }
						else backoff = std::min(backoff * 2, 4000);
						continue;
					}
					int64_t chunk = std::min<int64_t>({ n - got, (int64_t)std::max<uint32_t>(smb2_get_max_read_size(ctx), 4096), (int64_t)1 << 20 });
					int status = 0;
					void* data = nullptr;
					bool lost = false;
					NetResult r = link.call(ctx, *waiter, "Reading " + sub,
						[&](Call* k) { return smb2_pread_async(ctx, fh, (uint8_t*)buf + got, (uint32_t)chunk, (uint64_t)(offset + got), answered, k); },
						status, data, lost);
					if (r.ok() && status < 0) r = describe(ctx, "Reading " + sub, Wait::Answered, status, link.timeoutMs);
					if (!r.ok())
					{
						last = r;
						if (r.error == NetError::Cancelled) { release(); return -1; }	// (dead after: the caller closes it)
						if (lost || transient(r.error)) { release(); continue; }
						return got ? got : -1;
					}
					if (status == 0) break;	// the end of the file
					got += status;
				}
				last = NetResult();
				return got;
			}

			int64_t size() const override { return length; }
			void cancel() override { own.cancel(); }
			NetResult lastResult() const override { return last; }

		private:
			Link link;
			std::string share, sub;
			Canceller own;
			Canceller* waiter = &own;	// the backend's while it opens the file
			smb2_context* ctx = nullptr;
			smb2fh* fh = nullptr;
			int64_t length = -1;
			NetResult last;

			// a connection of its own, the share, the file (and its size, the first time)
			NetResult reopen(bool first)
			{
				release();
				NetResult r;
				smb2_context* c = link.open(share, *waiter, r);
				if (!c) return r;
				int status = 0;
				void* data = nullptr;
				bool lost = false;
				r = link.call(c, *waiter, "Opening " + sub, [&](Call* k) { return smb2_open_async(c, sub.c_str(), O_RDONLY, answered, k); }, status, data, lost);
				if (r.ok() && (status != 0 || !data)) r = describe(c, "Opening " + sub, Wait::Answered, status ? status : -EIO, link.timeoutMs);
				if (r.ok() && first)
				{
					smb2_stat_64 st = {};
					r = link.call(c, *waiter, "Sizing " + sub, [&](Call* k) { return smb2_fstat_async(c, (smb2fh*)data, &st, answered, k); }, status, data, lost);
					if (r.ok() && status != 0) r = describe(c, "Sizing " + sub, Wait::Answered, status, link.timeoutMs);
					if (r.ok()) length = (int64_t)st.smb2_size;
				}
				if (!r.ok()) { smb2_destroy_context(c); return r; }
				ctx = c;
				fh = (smb2fh*)data;
				return r;
			}

			// (the handle goes with its context: no close round trip, which a lost connection couldn't make, and
			// the server drops the handle with the session)
			void release()
			{
				if (ctx) smb2_destroy_context(ctx);
				ctx = nullptr;
				fh = nullptr;
			}
		};

		class SmbBackend : public NetBackend
		{
		public:
			SmbBackend(const NetServer& s, const std::string& password, int timeoutMs) : link(s, password, timeoutMs) {}
			~SmbBackend() override { for (auto& t : trees) release(t); }

			NetResult connect() override
			{
				Busy b(canceller);
				NetResult r;
				tree("IPC$", r);	// the login, and the tree the shares' list comes through
				return r;
			}

			NetResult list(const std::string& dir, std::vector<NetEntry>& out) override
			{
				Busy b(canceller);
				std::string share, sub;
				split(dir, share, sub);
				if (share.empty()) return shares(out);
				NetResult r;
				smb2_context* c = tree(share, r);
				if (!c) return r;
				int status = 0;
				void* data = nullptr;
				bool lost = false;
				std::string what = "Listing " + dir;
				r = link.call(c, canceller, what, [&](Call* k) { return smb2_opendir_async(c, sub.c_str(), answered, k); }, status, data, lost,
					[](smb2_context* x, void* d) { smb2_closedir(x, (smb2dir*)d); });
				if (r.ok() && (status != 0 || !data)) r = describe(c, what, Wait::Answered, status ? status : -EIO, link.timeoutMs);
				if (!r.ok()) return fail(c, r, lost);
				smb2dir* d = (smb2dir*)data;
				while (smb2dirent* e = smb2_readdir(c, d))
				{
					if (!strcmp(e->name, ".") || !strcmp(e->name, "..")) continue;
					NetEntry x;
					x.name = e->name;
					// (a reparse point is listed as a folder: it is a junction or a link more often than not)
					x.dir = e->st.smb2_type == SMB2_TYPE_DIRECTORY || e->st.smb2_type == SMB2_TYPE_LINK;
					x.size = x.dir ? -1 : (int64_t)e->st.smb2_size;
					out.push_back(x);
				}
				smb2_closedir(c, d);
				return r;
			}

			std::unique_ptr<NetFile> open(const std::string& path, NetResult& r) override
			{
				Busy b(canceller);
				std::string share, sub;
				split(path, share, sub);
				if (share.empty() || sub.empty()) { r = result(NetError::NotFound, path + " is not a file"); return nullptr; }
				std::unique_ptr<SmbFile> f(new SmbFile(link, share, sub));
				r = f->start(canceller);
				if (!r.ok()) return nullptr;
				return std::unique_ptr<NetFile>(f.release());
			}

			void cancel() override { canceller.cancel(); }

			NetResult makeDir(const std::string& path) override
			{
				return simple(path, "Making the folder ", [](smb2_context* c, const char* sub, Call* k) { return smb2_mkdir_async(c, sub, answered, k); });
			}

			NetResult remove(const std::string& path, bool dir) override
			{
				if (dir) return simple(path, "Deleting the folder ", [](smb2_context* c, const char* sub, Call* k) { return smb2_rmdir_async(c, sub, answered, k); });
				return simple(path, "Deleting ", [](smb2_context* c, const char* sub, Call* k) { return smb2_unlink_async(c, sub, answered, k); });
			}

			NetResult rename(const std::string& from, const std::string& to) override
			{
				std::string s1, a, s2, b;
				split(from, s1, a);
				split(to, s2, b);
				if (s1.empty() || a.empty() || b.empty() || lower(s1) != lower(s2)) return result(NetError::Other, "Renaming " + from + ": not within one share");
				return simple(from, "Renaming ", [&b](smb2_context* c, const char* sub, Call* k) { return smb2_rename_async(c, sub, b.c_str(), answered, k); });
			}

			NetResult put(const std::string& path, const std::function<int64_t(char*, int64_t)>& more) override
			{
				Busy b(canceller);
				std::string share, sub;
				split(path, share, sub);
				if (share.empty() || sub.empty()) return result(NetError::NoAccess, path + ": a file can't go there");
				NetResult r;
				smb2_context* c = tree(share, r);
				if (!c) return r;
				int status = 0;
				void* data = nullptr;
				bool lost = false;
				std::string what = "Writing " + path;
				r = link.call(c, canceller, what, [&](Call* k) { return smb2_open_async(c, sub.c_str(), O_WRONLY | O_CREAT | O_EXCL, answered, k); },
					status, data, lost);
				if (r.ok() && (status != 0 || !data)) r = describe(c, what, Wait::Answered, status ? status : -EIO, link.timeoutMs);
				if (!r.ok()) return fail(c, r, lost);
				smb2fh* fh = (smb2fh*)data;
				int64_t chunk = std::min<int64_t>((int64_t)std::max<uint32_t>(smb2_get_max_write_size(c), 65536), (int64_t)1 << 20);
				std::vector<char> buf((size_t)chunk);
				uint64_t offset = 0;
				bool stopped = false;
				for (;;)
				{
					int64_t n = more(buf.data(), chunk);
					if (n < 0) { stopped = true; break; }
					if (n == 0) break;
					int64_t w = 0;
					while (w < n)
					{
						r = link.call(c, canceller, what, [&](Call* k) { return smb2_pwrite_async(c, fh, (const uint8_t*)buf.data() + w, (uint32_t)(n - w), offset, answered, k); },
							status, data, lost);
						if (r.ok() && status <= 0) r = describe(c, what, Wait::Answered, status < 0 ? status : -EIO, link.timeoutMs);
						if (!r.ok()) break;
						w += status;
						offset += (uint64_t)status;
					}
					if (!r.ok()) break;
				}
				if (lost || r.error == NetError::Cancelled)
				{
					// (the context is dead: a new one takes the half file away)
					fail(c, r, true);
					NetResult gone;
					if (smb2_context* c2 = tree(share, gone))
					{
						int st2 = 0; void* d2 = nullptr; bool l2 = false;
						Canceller fresh;
						link.call(c2, fresh, "Deleting " + path, [&](Call* k) { return smb2_unlink_async(c2, sub.c_str(), answered, k); }, st2, d2, l2);
					}
					return r;
				}
				NetResult closed = link.call(c, canceller, what, [&](Call* k) { return smb2_close_async(c, fh, answered, k); }, status, data, lost);
				if (r.ok() && closed.ok() && status < 0) closed = describe(c, what, Wait::Answered, status, link.timeoutMs);
				if (r.ok() && !closed.ok()) r = closed;
				if (!r.ok() || stopped)
				{
					int st2 = 0; void* d2 = nullptr; bool l2 = false;
					link.call(c, canceller, "Deleting " + path, [&](Call* k) { return smb2_unlink_async(c, sub.c_str(), answered, k); }, st2, d2, l2);
					if (r.ok()) r = result(NetError::Cancelled, what + ": stopped");
					return fail(c, r, lost);
				}
				return r;
			}

		private:
			// one request on a path's share, its answer's status the result
			template <class Start> NetResult simple(const std::string& path, const std::string& verb, Start start)
			{
				Busy b(canceller);
				std::string share, sub;
				split(path, share, sub);
				if (share.empty() || sub.empty()) return result(NetError::NoAccess, verb + path + ": not inside a share");
				NetResult r;
				smb2_context* c = tree(share, r);
				if (!c) return r;
				int status = 0;
				void* data = nullptr;
				bool lost = false;
				std::string what = verb + path;
				r = link.call(c, canceller, what, [&](Call* k) { return start(c, sub.c_str(), k); }, status, data, lost);
				if (r.ok() && status != 0) r = describe(c, what, Wait::Answered, status, link.timeoutMs);
				if (!r.ok()) return fail(c, r, lost);
				return r;
			}

			struct Tree { std::string share; smb2_context* ctx; bool dead; };
			Link link;
			std::vector<Tree> trees;
			Canceller canceller;

			static std::string lower(std::string s)
			{
				for (char& c : s) c = (char)tolower((unsigned char)c);
				return s;
			}

			// a failed request's result; the tree is dropped when the connection is gone or the server let it go
			NetResult fail(smb2_context* c, const NetResult& r, bool lost)
			{
				if (r.error == NetError::Cancelled || lost || transient(r.error))
					for (auto& t : trees) if (t.ctx == c) t.dead = true;
				return r;
			}

			// a tree let go of: its connection closed, no goodbye. libsmb2's disconnect waits for the server's answer
			// (a sync call: none of the cancel's), and a server that stopped answering held the player's quit till
			// it timed out; the server drops the session with the connection anyway
			static void release(const Tree& t)
			{
				smb2_destroy_context(t.ctx);
			}

			// the share's tree, connected the first time, and again once its connection was lost
			smb2_context* tree(const std::string& share, NetResult& r)
			{
				std::string key = lower(share);
				for (size_t i = 0; i < trees.size(); i++)
				{
					if (trees[i].share != key) continue;
					if (!trees[i].dead) return trees[i].ctx;
					release(trees[i]);
					trees.erase(trees.begin() + (long)i);
					break;
				}
				smb2_context* c = link.open(share, canceller, r);
				if (c) trees.push_back({ key, c, false });
				return c;
			}

			// the disk shares (not the hidden ones: printers, IPC$, ADMIN$, C$...)
			NetResult shares(std::vector<NetEntry>& out)
			{
				NetResult r;
				smb2_context* c = tree("IPC$", r);
				if (!c) return r;
				const std::string what = "Listing the shares of " + link.server.host;
				int status = 0;
				void* data = nullptr;
				bool lost = false;
				r = link.call(c, canceller, what, [&](Call* k) { return smb2_share_enum_async(c, SHARE_INFO_1, answered, k); }, status, data, lost,
					[](smb2_context* x, void* d) { smb2_free_data(x, d); });
				if (r.ok() && (status != 0 || !data)) r = describe(c, what, Wait::Answered, status ? status : -EIO, link.timeoutMs);
				if (!r.ok()) return fail(c, r, lost);
				auto* rep = (srvsvc_NetrShareEnum_rep*)data;
				for (uint32_t i = 0; i < rep->ses.ShareInfo.Level1.EntriesRead; i++)
				{
					const auto& si = rep->ses.ShareInfo.Level1.Buffer->share_info_1[i];
					std::string name = si.netname.utf8 ? si.netname.utf8 : "";
					if ((si.type & 3) != SHARE_TYPE_DISKTREE || (si.type & SHARE_TYPE_HIDDEN) || name.empty() || name.back() == '$') continue;
					NetEntry e;
					e.name = name;
					e.dir = true;
					out.push_back(e);
				}
				smb2_free_data(c, rep);
				return r;
			}
		};
	}

	std::unique_ptr<NetBackend> makeSmbBackend(const NetServer& server, const std::string& password, int timeoutMs)
	{
		return std::unique_ptr<NetBackend>(new SmbBackend(server, password, timeoutMs));
	}
}
