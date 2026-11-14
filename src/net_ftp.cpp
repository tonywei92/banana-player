// FTP and explicit FTPS through libcurl's multi interface, which is polled here (waits can be cancelled from another
// thread, and a server that stops answering is given up on after the timeout, not when TCP does).
//
// One Session is one curl handle with its connection. A listing and a file's size are one transfer each, run to the end
// (MLSD, or LIST for a server without it). A file is read by one RETR that runs from the offset being read: its data
// goes into a buffer of the Session (the transfer is paused when the buffer is full), a read at another place stops
// it and starts one at the new offset (REST), a small step forward is read and thrown away instead. A transfer that
// fails to the connection is started again from where it stopped, as SmbFile does: mpv takes a failed read for the end
// of the file, so the stream rides out a Wi-Fi hiccup itself (about 20 s).
//
// A Session's end closes its sockets without a goodbye: curl's QUIT waits for an answer from a server that may be gone
// (a quit held for seconds that way, with SMB), so the sockets are shut down first (curl makes them through
// opensocket here, to have them), and the QUIT fails at once.
#include "ftplist.h"
#include "net.h"
#include "netwait.h"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace bp
{
	namespace
	{
		using netwait::Busy;
		using netwait::Canceller;
		using netwait::nowMs;

		const int64_t kBuffer = 1 << 20;		// what a read-ahead keeps before the transfer is paused
		const int64_t kSkip = 1 << 20;			// a step forward this small is read through, not a new transfer
		const size_t kListLimit = 64u << 20;	// a listing longer than this isn't one

		bool transient(NetError e) { return e == NetError::Gone || e == NetError::Timeout; }

		std::string lower(std::string s)
		{
			for (char& c : s) c = (char)tolower((unsigned char)c);
			return s;
		}

		// one path segment as a URL has it: UTF-8 bytes as %XX, but for the unreserved ones
		std::string escape(const std::string& s)
		{
			static const char* hex = "0123456789ABCDEF";
			std::string out;
			for (unsigned char c : s)
			{
				if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') out += (char)c;
				else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
			}
			return out;
		}

		// the sockets curl opened for a Session, to shut down before it ends
		struct Socks
		{
			std::vector<int> fds;

			static curl_socket_t open(void* p, curlsocktype, curl_sockaddr* a)
			{
				int fd = socket(a->family, a->socktype, a->protocol);
				if (fd < 0) return CURL_SOCKET_BAD;
				fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
				int on = 1;
				setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
				((Socks*)p)->fds.push_back(fd);
				return fd;
			}
			static int close(void* p, curl_socket_t fd)
			{
				auto& v = ((Socks*)p)->fds;
				v.erase(std::remove(v.begin(), v.end(), (int)fd), v.end());
				::close((int)fd);
				return 0;
			}
			void shutdownAll() { for (int fd : fds) shutdown(fd, SHUT_RDWR); }
		};

		enum class Turn { More, Finished, Cancelled, Stalled };

		class Session
		{
		public:
			Session(const NetServer& server, const std::string& password, int timeoutMs) : server(server), password(password), timeoutMs(timeoutMs)
			{
				// (OpenSSL's writes to a connection the server dropped would end the player otherwise)
				signal(SIGPIPE, SIG_IGN);
			}
			~Session() { hardClose(); }
			Session(const Session&) = delete;
			Session& operator=(const Session&) = delete;

			const NetServer server;
			const std::string password;
			const int timeoutMs;

			// what the transfer gave so far: a listing's text, or a file's bytes from the buffer's head
			std::vector<char> buf;
			size_t head = 0;
			int64_t deadline = 0;	// when set, the time (nowMs) the transfer has to end by, whatever comes meanwhile
			int64_t pauseAt = 0;	// 0: no limit (a listing)
			bool paused = false;
			CURLcode code = CURLE_OK;
			char errbuf[CURL_ERROR_SIZE] = {};

			std::string entry;	// the login's folder ("/" on a server that confines its users), "" until known
			int64_t buffered() const { return (int64_t)(buf.size() - head); }
			const char* data() const { return buf.data() + head; }
			void consume(int64_t n)
			{
				head += (size_t)n;
				if (head == buf.size()) { buf.clear(); head = 0; }
				else if (head > (size_t)kBuffer) { buf.erase(buf.begin(), buf.begin() + (long)head); head = 0; }
			}
			void clear() { buf.clear(); head = 0; }

			std::string base() const
			{
				std::string h = server.host;
				if (h.find(':') != std::string::npos && h[0] != '[') h = "[" + h + "]";
				return "ftp://" + h + ":" + std::to_string(server.port ? server.port : 21);
			}
			// a server path ("/Media/My Films") as a URL, from the folder the login lands in: that is the root on a
			// server that confines its users to their home, which most NASes do, and a path from the real root
			// (curl's "//") would be refused by one that doesn't. A folder's ends with "/" (without it curl takes it
			// for a file)
			std::string url(const std::string& path, bool dir) const
			{
				std::string rest;
				size_t i = 0;
				while (i < path.size())
				{
					size_t e = path.find('/', i);
					if (e == std::string::npos) e = path.size();
					if (e > i) rest += (rest.empty() ? "" : "/") + escape(path.substr(i, e - i));
					i = e + 1;
				}
				if (dir && !rest.empty()) rest += "/";
				return base() + "/" + rest;
			}

			// a handle set up for one more transfer (its connection, if still good, is reused)
			CURL* prepare(const std::string& u)
			{
				if (!multi) multi = curl_multi_init();
				if (!easy) easy = curl_easy_init();
				else
				{
					detach();
					curl_easy_reset(easy);
				}
				errbuf[0] = 0;
				paused = false;
				deadline = 0;
				pauseAt = 0;
				curl_easy_setopt(easy, CURLOPT_URL, u.c_str());
				curl_easy_setopt(easy, CURLOPT_USERNAME, server.guest ? "anonymous" : server.user.c_str());
				curl_easy_setopt(easy, CURLOPT_PASSWORD, server.guest ? "anonymous@" : password.c_str());
				curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
				curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, errbuf);
				curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, (long)timeoutMs);
				curl_easy_setopt(easy, CURLOPT_FTP_RESPONSE_TIMEOUT, (long)std::max(1, timeoutMs / 1000));
				curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
				curl_easy_setopt(easy, CURLOPT_BUFFERSIZE, 256L * 1024);
				curl_easy_setopt(easy, CURLOPT_FTP_FILEMETHOD, (long)CURLFTPMETHOD_SINGLECWD);
				// (a NAS behind NAT or in a container often names an address of its own in the PASV reply)
				curl_easy_setopt(easy, CURLOPT_FTP_SKIP_PASV_IP, 1L);
				curl_easy_setopt(easy, CURLOPT_OPENSOCKETFUNCTION, &Socks::open);
				curl_easy_setopt(easy, CURLOPT_OPENSOCKETDATA, &socks);
				curl_easy_setopt(easy, CURLOPT_CLOSESOCKETFUNCTION, &Socks::close);
				curl_easy_setopt(easy, CURLOPT_CLOSESOCKETDATA, &socks);
				curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &Session::onWrite);
				curl_easy_setopt(easy, CURLOPT_WRITEDATA, this);
				// names in UTF-8 (a server that doesn't know the command says so, and that's all right)
				if (quote) curl_slist_free_all(quote);
				quote = curl_slist_append(nullptr, "*OPTS UTF8 ON");
				curl_easy_setopt(easy, CURLOPT_QUOTE, quote);
				if (server.ftps)
				{
					curl_easy_setopt(easy, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
					// home servers have certificates they made themselves, which no store knows: the connection is
					// encrypted, not authenticated, as a saved connection to a NAS in the house is
					curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
					curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);
				}
				return easy;
			}

			void begin()
			{
				curl_multi_add_handle(multi, easy);
				attached = true;
				code = CURLE_OK;
				lastLife = nowMs();
			}

			// one turn of waiting for the transfer: as long as data comes, no wait at all
			Turn turn(Canceller& cancel)
			{
				if (cancel.cancelled()) return Turn::Cancelled;
				int64_t before = rx;
				int running = 0;
				CURLMcode mc = curl_multi_perform(multi, &running);
				int left = 0;
				while (CURLMsg* m = curl_multi_info_read(multi, &left))
				{
					if (m->msg != CURLMSG_DONE || m->easy_handle != easy) continue;
					code = m->data.result;
					detach();
					return Turn::Finished;
				}
				if (mc != CURLM_OK || !running)
				{
					code = CURLE_RECV_ERROR;
					snprintf(errbuf, sizeof errbuf, "%s", mc != CURLM_OK ? curl_multi_strerror(mc) : "the transfer ended without a result");
					detach();
					return Turn::Finished;
				}
				int64_t now = nowMs();
				if (deadline && now >= deadline) return Turn::Stalled;
				if (rx != before) { lastLife = now; return Turn::More; }
				if (paused) lastLife = now;
				int64_t wait = lastLife + timeoutMs - now;
				if (wait <= 0) return Turn::Stalled;
				curl_waitfd extra = { cancel.fd(), CURL_WAIT_POLLIN, 0 };
				curl_multi_poll(multi, &extra, cancel.fd() >= 0 ? 1 : 0, (int)std::min<int64_t>(wait, 100), nullptr);
				return Turn::More;
			}

			// a command sent before the transfer (MKD, DELE, RNFR + RNTO...), after the prepare() that set the list up
			void addQuote(const std::string& cmd)
			{
				quote = curl_slist_append(quote, cmd.c_str());
				curl_easy_setopt(easy, CURLOPT_QUOTE, quote);
			}
			// an upload's bytes sent: signs of life too, as a download's received are
			void alive(int64_t n) { rx += n; }

			void resume()
			{
				if (!paused) return;
				paused = false;
				lastLife = nowMs();
				curl_easy_pause(easy, CURLPAUSE_CONT);
			}

			// the transfer to its end (a listing, a size): nothing left of the handle when it fails
			NetResult run(Canceller& cancel, const std::string& what)
			{
				begin();
				for (;;)
				{
					switch (turn(cancel))
					{
					case Turn::More: continue;
					case Turn::Finished:
					{
						NetResult r = describe(what, code);
						// the folder the login lands in, once: the commands' paths start from it (the connection's
						// own folder is wherever its last transfer went)
						if (r.ok() && entry.empty())
						{
							char* ep = nullptr;
							if (curl_easy_getinfo(easy, CURLINFO_FTP_ENTRY_PATH, &ep) == CURLE_OK && ep) entry = ep;
						}
						if (!r.ok()) hardClose();
						return r;
					}
					case Turn::Cancelled:
						hardClose();
						return failure(NetError::Cancelled, what + ": cancelled");
					case Turn::Stalled:
						hardClose();
						return failure(NetError::Timeout, what + ": no answer from " + server.host);
					}
				}
			}

			static NetResult failure(NetError e, const std::string& detail)
			{
				NetResult r;
				r.error = e;
				r.detail = detail;
				return r;
			}

			// what a finished transfer's code means
			NetResult describe(const std::string& what, CURLcode c)
			{
				if (c == CURLE_OK) return NetResult();
				long os = 0;
				curl_easy_getinfo(easy, CURLINFO_OS_ERRNO, &os);
				std::string text = errbuf[0] ? errbuf : curl_easy_strerror(c);
				std::string low = lower(text);
				std::string detail = what + ": " + text;
				bool denied = low.find("permission") != std::string::npos || low.find("denied") != std::string::npos ||
					low.find("not allowed") != std::string::npos || low.find("access") != std::string::npos;
				switch (c)
				{
				case CURLE_LOGIN_DENIED: return failure(NetError::Auth, detail);
				// a command or an upload the server refused: 550/553 (no permission, a name taken) mostly
				case CURLE_QUOTE_ERROR:
				case CURLE_UPLOAD_FAILED:
				case CURLE_REMOTE_DISK_FULL:
					return failure(c == CURLE_REMOTE_DISK_FULL ? NetError::Other : NetError::NoAccess, detail);
				case CURLE_REMOTE_ACCESS_DENIED: return failure(NetError::NotFound, detail);	// (the CWD was refused: curl gives no reply text, and a folder that is not there is the usual cause)
				case CURLE_REMOTE_FILE_NOT_FOUND: return failure(denied ? NetError::NoAccess : NetError::NotFound, detail);
				case CURLE_FTP_COULDNT_RETR_FILE:
					if (low.find("530") != std::string::npos) return failure(NetError::Auth, detail);
					return failure(denied ? NetError::NoAccess : NetError::NotFound, detail);
				case CURLE_COULDNT_RESOLVE_HOST:
				case CURLE_COULDNT_RESOLVE_PROXY: return failure(NetError::NotFound, detail);
				case CURLE_COULDNT_CONNECT:
					if (os == ENETUNREACH || os == ENETDOWN) return failure(NetError::NoNetwork, detail);
					if (os == ETIMEDOUT) return failure(NetError::Timeout, detail);
					return failure(NetError::NotFound, detail);	// refused, or no route to that host
				case CURLE_OPERATION_TIMEDOUT:
				case CURLE_FTP_ACCEPT_TIMEOUT: return failure(NetError::Timeout, detail);
				case CURLE_RECV_ERROR:
				case CURLE_SEND_ERROR:
				case CURLE_PARTIAL_FILE:
				case CURLE_GOT_NOTHING:
				case CURLE_FTP_WEIRD_SERVER_REPLY: return failure(NetError::Gone, detail);
				default: return failure(NetError::Other, detail);
				}
			}

			// the connection and the handle gone, no goodbye (the buffer stays: its bytes are still the file's)
			void hardClose()
			{
				socks.shutdownAll();
				detach();
				if (easy) curl_easy_cleanup(easy);
				if (multi) curl_multi_cleanup(multi);
				easy = nullptr;
				multi = nullptr;
				if (quote) curl_slist_free_all(quote);
				quote = nullptr;
				paused = false;
			}

		private:
			CURL* easy = nullptr;
			CURLM* multi = nullptr;
			curl_slist* quote = nullptr;
			Socks socks;
			bool attached = false;
			int64_t rx = 0, lastLife = 0;

			void detach()
			{
				if (attached && multi && easy) curl_multi_remove_handle(multi, easy);
				attached = false;
			}

			static size_t onWrite(char* p, size_t size, size_t n, void* user)
			{
				Session* s = (Session*)user;
				size_t len = size * n;
				if (s->pauseAt) { if (s->buffered() >= s->pauseAt) { s->paused = true; return CURL_WRITEFUNC_PAUSE; } }
				else if (s->buf.size() + len > kListLimit) return 0;
				s->buf.insert(s->buf.end(), p, p + len);
				s->rx += (int64_t)len;
				return len;
			}
		};

		// a folder's entries: MLSD, and LIST once a server has refused it (noMlsd keeps that for the next time)
		NetResult listFolder(Session& s, Canceller& cancel, const std::string& path, bool& noMlsd, std::vector<NetEntry>& out)
		{
			const std::string what = "Listing " + path + " on " + s.server.host;
			for (int pass = noMlsd ? 1 : 0; pass < 2; pass++)
			{
				CURL* e = s.prepare(s.url(path, true));
				s.clear();
				if (pass == 0) curl_easy_setopt(e, CURLOPT_CUSTOMREQUEST, "MLSD");
				NetResult r = s.run(cancel, what);
				if (r.ok())
				{
					out = pass == 0 ? parseMlsd(std::string(s.data(), (size_t)s.buffered())) : parseFtpList(std::string(s.data(), (size_t)s.buffered()));
					s.clear();
					return r;
				}
				s.clear();
				// the server's "no such command" (500, 502, 504) is a failed listing to curl
				if (pass == 0 && s.code == CURLE_FTP_COULDNT_RETR_FILE && r.error != NetError::Auth) { noMlsd = true; continue; }
				return r;
			}
			return NetResult();
		}

		class FtpFile : public NetFile
		{
		public:
			FtpFile(const NetServer& server, const std::string& password, int timeoutMs, const std::string& path)
				: s(server, password, timeoutMs), path(path) {}

			// the first open (the size), on the backend's thread and under its cancel (the file's own takes over after)
			NetResult start(Canceller& backend)
			{
				waiter = &backend;
				NetResult r = measure();
				waiter = &own;
				last = r;
				return r;
			}

			int64_t read(char* buf, int64_t n, int64_t offset) override
			{
				Busy b(own);
				if (n <= 0) return 0;
				if (offset >= length || offset < 0) { last = NetResult(); return 0; }
				n = std::min(n, length - offset);
				if (s.paused && s.buffered() < kBuffer / 2) s.resume();
				int64_t got = 0, droppedAt = 0;
				int dropped = 0;	// 1: lost once (tried again at once), 2: again (with a pause)
				int backoff = 1000;
				while (got < n)
				{
					int64_t want = offset + got;
					// the bytes in the buffer are the file's from bufOff; where the transfer goes on from them,
					// reading on costs nothing, and anything else is a seek
					if (!valid || want < bufOff || want > bufOff + s.buffered() + kSkip)
					{
						if (active) s.hardClose();
						s.clear();
						bufOff = want;
						valid = true;
						active = atEnd = false;
					}
					if (s.buffered() > 0)
					{
						int64_t skip = want - bufOff;
						int64_t k = std::min(skip > 0 ? skip : n - got, s.buffered());
						if (skip <= 0) memcpy(buf + got, s.data(), (size_t)k);
						s.consume(k);
						bufOff += k;
						if (skip <= 0) got += k;
						continue;
					}
					if (atEnd) break;
					if (s.paused) s.resume();
					if (!active)
					{
						// a transfer from where the buffer ends; a second try after a failure waits first
						if (dropped > 1)
						{
							if (!waiter->sleep(backoff)) { last = Session::failure(NetError::Cancelled, "Reading " + path + ": cancelled"); return -1; }
							backoff = std::min(backoff * 2, 4000);
						}
						CURL* e = s.prepare(s.url(path, false));
						if (bufOff > 0) curl_easy_setopt(e, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)bufOff);
						s.pauseAt = kBuffer;
						if (dropped) s.deadline = droppedAt + 20000;	// (a try never goes on past the time the tries end)
						s.begin();
						active = true;
					}
					NetResult r;
					switch (s.turn(*waiter))
					{
					case Turn::More:
						if (s.buffered() > 0) { dropped = 0; backoff = 1000; }
						continue;
					case Turn::Finished:
						active = false;
						r = s.describe("Reading " + path, s.code);
						if (r.ok()) { atEnd = true; continue; }
						break;
					case Turn::Cancelled:
						s.hardClose();
						active = false;
						last = Session::failure(NetError::Cancelled, "Reading " + path + ": cancelled");
						return -1;
					case Turn::Stalled:
						r = Session::failure(NetError::Timeout, "Reading " + path + ": no answer from " + s.server.host);
						break;
					}
					// a transfer that failed
					s.hardClose();
					active = false;
					last = r;
					if (!transient(r.error) && r.error != NetError::NotFound) return got ? got : -1;
					int64_t now = nowMs();
					// (a stall has already waited the timeout out: the 20 s count from the last sign of life)
					if (!dropped) { dropped = 1; droppedAt = r.error == NetError::Timeout ? now - s.timeoutMs : now; }
					else if (now - droppedAt >= 20000)
					{
						last = Session::failure(NetError::Gone, "Reading " + path + ": the connection was lost and not got back");
						return got ? got : -1;
					}
					else dropped = 2;
				}
				last = NetResult();
				return got;
			}

			int64_t size() const override { return length; }
			void cancel() override { own.cancel(); }
			NetResult lastResult() const override { return last; }

		private:
			Session s;
			std::string path;
			Canceller own;
			Canceller* waiter = &own;	// the backend's while it opens the file
			int64_t length = -1;
			int64_t bufOff = 0;			// the file's offset of the buffer's first byte
			bool valid = false;			// bufOff means something: a read has been made
			bool active = false;		// a transfer is running
			bool atEnd = false;			// it ended well: nothing more comes
			NetResult last;

			// the size, from SIZE; a server without it is asked for the folder
			NetResult measure()
			{
				CURL* e = s.prepare(s.url(path, false));
				curl_easy_setopt(e, CURLOPT_NOBODY, 1L);
				NetResult r = s.run(*waiter, "Opening " + path);
				if (!r.ok()) return r;
				curl_off_t len = -1;
				curl_easy_getinfo(e, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &len);
				if (len >= 0) { length = len; return r; }
				size_t slash = path.rfind('/');
				std::string dir = slash == std::string::npos ? "/" : path.substr(0, slash), name = path.substr(slash == std::string::npos ? 0 : slash + 1);
				std::vector<NetEntry> v;
				bool noMlsd = false;
				r = listFolder(s, *waiter, dir, noMlsd, v);
				if (!r.ok()) return r;
				for (const NetEntry& x : v)
					if (x.name == name && !x.dir && x.size >= 0) { length = x.size; return r; }
				return Session::failure(NetError::NotFound, "Opening " + path + ": no such file");
			}
		};

		class FtpBackend : public NetBackend
		{
		public:
			FtpBackend(const NetServer& server, const std::string& password, int timeoutMs) : s(server, password, timeoutMs) {}

			NetResult connect() override
			{
				Busy b(canceller);
				CURL* e = s.prepare(s.url("/", true));
				curl_easy_setopt(e, CURLOPT_NOBODY, 1L);	// the login and the folder, no listing
				return s.run(canceller, "Logging in to " + s.server.host);
			}

			NetResult list(const std::string& path, std::vector<NetEntry>& out) override
			{
				Busy b(canceller);
				return listFolder(s, canceller, path, noMlsd, out);
			}

			std::unique_ptr<NetFile> open(const std::string& path, NetResult& result) override
			{
				Busy b(canceller);
				std::unique_ptr<FtpFile> f(new FtpFile(s.server, s.password, s.timeoutMs, path));
				result = f->start(canceller);
				if (!result.ok()) return nullptr;
				return f;
			}

			void cancel() override { canceller.cancel(); }

			NetResult makeDir(const std::string& path) override { return command(path, {{"MKD ", path}}, "Making the folder "); }
			NetResult remove(const std::string& path, bool dir) override
			{
				return command(path, {{dir ? "RMD " : "DELE ", path}}, dir ? "Deleting the folder " : "Deleting ");
			}
			NetResult rename(const std::string& from, const std::string& to) override
			{
				// (RNTO replaces a file on most servers: a name taken is refused here first)
				NetResult r = taken(to);
				if (!r.ok()) return r;
				return command(from, {{"*RNFR ", from}, {"RNTO ", to}}, "Renaming ");	// (* : RNFR's 350 is no failure)
			}

			NetResult put(const std::string& path, const std::function<int64_t(char*, int64_t)>& more) override
			{
				NetResult r = taken(path);
				if (!r.ok()) return r;
				Busy b(canceller);
				struct Feed { const std::function<int64_t(char*, int64_t)>* more; Session* s; bool stopped = false; };
				Feed feed{&more, &s};
				CURL* e = s.prepare(s.url(path, false));
				curl_easy_setopt(e, CURLOPT_UPLOAD, 1L);
				curl_easy_setopt(e, CURLOPT_READFUNCTION, (curl_read_callback)[](char* buf, size_t size, size_t n, void* user) -> size_t {
					Feed* f = (Feed*)user;
					int64_t got = (*f->more)(buf, (int64_t)(size * n));
					if (got < 0) { f->stopped = true; return CURL_READFUNC_ABORT; }
					f->s->alive(got);
					return (size_t)got;
				});
				curl_easy_setopt(e, CURLOPT_READDATA, &feed);
				r = s.run(canceller, "Writing " + path);
				if (r.ok() && !feed.stopped) return r;
				// the half file goes (on a connection of its own: this one may be gone)
				Canceller fresh;
				CURL* d = s.prepare(s.url("/", true));
				curl_easy_setopt(d, CURLOPT_NOBODY, 1L);
				if (!s.entry.empty()) s.addQuote("DELE " + rel(path));
				s.run(fresh, "Deleting " + path);
				if (r.ok()) r = Session::failure(NetError::Cancelled, "Writing " + path + ": stopped");
				return r;
			}

		private:
			// a path as the commands take it: whole, from the folder the login lands in (as url() reads paths)
			std::string rel(const std::string& path) const
			{
				size_t from = path.find_first_not_of('/');
				std::string r = from == std::string::npos ? "" : path.substr(from);
				std::string base = s.entry.empty() ? "/" : s.entry;
				if (base.back() != '/') base += '/';
				return r.empty() ? "" : base + r;
			}
			// the login's folder known (a login that does nothing else)
			NetResult entered()
			{
				if (!s.entry.empty()) return NetResult();
				CURL* e = s.prepare(s.url("/", true));
				curl_easy_setopt(e, CURLOPT_NOBODY, 1L);
				return s.run(canceller, "Logging in to " + s.server.host);
			}

			// each command: its verb and its path (made whole once the login's folder is known)
			NetResult command(const std::string& path, const std::vector<std::pair<std::string, std::string>>& cmds, const std::string& verb)
			{
				Busy b(canceller);
				NetResult r0 = entered();
				if (!r0.ok()) return r0;
				if (path.find_first_not_of('/') == std::string::npos) return Session::failure(NetError::NoAccess, verb + path + ": the top folder");
				CURL* e = s.prepare(s.url("/", true));
				curl_easy_setopt(e, CURLOPT_NOBODY, 1L);
				for (const auto& c : cmds) s.addQuote(c.first + rel(c.second));
				return s.run(canceller, verb + path);
			}

			// NoAccess when something already has that name (its folder listed)
			NetResult taken(const std::string& path)
			{
				size_t slash = path.rfind('/');
				std::string dir = slash == std::string::npos || slash == 0 ? "/" : path.substr(0, slash), name = path.substr(slash == std::string::npos ? 0 : slash + 1);
				std::vector<NetEntry> v;
				NetResult r = list(dir, v);
				if (!r.ok()) return r;
				for (const NetEntry& x : v)
					if (x.name == name) return Session::failure(NetError::NoAccess, path + ": something already has that name");
				return r;
			}

			Session s;
			Canceller canceller;
			bool noMlsd = false;
		};
	}

	std::unique_ptr<NetBackend> makeFtpBackend(const NetServer& server, const std::string& password, int timeoutMs)
	{
		static const bool once = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
		(void)once;
		return std::unique_ptr<NetBackend>(new FtpBackend(server, password, timeoutMs));
	}
}
