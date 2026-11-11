#include "javaext.h"
#include "tarx.h"
#include "update.h"
#include <curl/curl.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace bp
{
	namespace
	{
		constexpr const char* MARKER = "installed";
		constexpr const char* SERVER_JAR = "server.jar";
		constexpr const char* JRE_TAR = "jre.tar.gz";
		constexpr const char* JRE_DIR = "jre";
		constexpr const char* JRE_STAGE = "jre.tmp";
		constexpr const char* SERVER_LOG = "server.log";

		int64_t sdlNow()
		{
			return (int64_t)SDL_GetTicks();
		}

		std::string clockStamp()
		{
			struct timeval tv;
			gettimeofday(&tv, nullptr);
			struct tm t;
			localtime_r(&tv.tv_sec, &t);
			char b[32];
			snprintf(b, sizeof b, "%02d:%02d:%02d.%03d", t.tm_hour, t.tm_min, t.tm_sec, (int)(tv.tv_usec / 1000));
			return b;
		}

		const char* stateName(JavaExtHost::State s)
		{
			switch (s)
			{
			case JavaExtHost::State::NotInstalled: return "not installed";
			case JavaExtHost::State::Downloading: return "downloading";
			case JavaExtHost::State::Installed: return "installed";
			case JavaExtHost::State::Starting: return "starting";
			case JavaExtHost::State::Running: return "running";
			case JavaExtHost::State::Failed: return "failed";
			}
			return "?";
		}

		bool isDir(const std::string& p)
		{
			struct stat st;
			return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
		}

		bool isFile(const std::string& p)
		{
			struct stat st;
			return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
		}

		int64_t fileSize(const std::string& p)
		{
			struct stat st;
			return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? (int64_t)st.st_size : 0;
		}

		bool ensureDir(const std::string& path)
		{
			if (path.empty()) return false;
			if (isDir(path)) return true;
			size_t slash = path.find_last_of('/');
			if (slash != std::string::npos && slash > 0 && !ensureDir(path.substr(0, slash))) return false;
			return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
		}

		bool removeTree(const std::string& path)
		{
			struct stat st;
			if (lstat(path.c_str(), &st) != 0) return errno == ENOENT;
			if (!S_ISDIR(st.st_mode)) return unlink(path.c_str()) == 0 || errno == ENOENT;
			DIR* d = opendir(path.c_str());
			if (!d) return false;
			bool ok = true;
			while (struct dirent* e = readdir(d))
			{
				if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
				if (!removeTree(path + "/" + e->d_name)) ok = false;
			}
			closedir(d);
			return rmdir(path.c_str()) == 0 && ok;
		}

		int64_t treeSize(const std::string& path)
		{
			struct stat st;
			if (lstat(path.c_str(), &st) != 0) return 0;
			if (!S_ISDIR(st.st_mode)) return S_ISREG(st.st_mode) ? (int64_t)st.st_size : 0;
			int64_t total = 0;
			DIR* d = opendir(path.c_str());
			if (!d) return 0;
			while (struct dirent* e = readdir(d))
			{
				if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
				total += treeSize(path + "/" + e->d_name);
			}
			closedir(d);
			return total;
		}

		std::vector<std::string> listDir(const std::string& path)
		{
			std::vector<std::string> out;
			DIR* d = opendir(path.c_str());
			if (!d) return out;
			while (struct dirent* e = readdir(d))
				if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) out.push_back(e->d_name);
			closedir(d);
			std::sort(out.begin(), out.end());
			return out;
		}

		bool readMarker(const std::string& javaDir, int64_t& bytes)
		{
			std::ifstream in(javaDir + "/" + MARKER);
			std::string tag;
			if (!(in >> tag >> bytes)) return false;
			return tag == "bp-javaext";
		}

		// written only once both files are whole and the runtime is unpacked: its presence is "installed"
		bool writeMarker(const std::string& javaDir, int64_t bytes)
		{
			std::string tmp = javaDir + "/" + MARKER + ".tmp";
			FILE* f = fopen(tmp.c_str(), "wb");
			if (!f) return false;
			bool ok = fprintf(f, "bp-javaext %lld\n", (long long)bytes) > 0;
			if (fclose(f) != 0) ok = false;
			if (!ok || rename(tmp.c_str(), (javaDir + "/" + MARKER).c_str()) != 0)
			{
				unlink(tmp.c_str());
				return false;
			}
			return true;
		}

		int freePort()
		{
			int s = socket(AF_INET, SOCK_STREAM, 0);
			if (s < 0) return 0;
			struct sockaddr_in a {};
			a.sin_family = AF_INET;
			a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			a.sin_port = 0;
			socklen_t n = (socklen_t)sizeof a;
			int port = 0;
			if (bind(s, (struct sockaddr*)&a, sizeof a) == 0 && getsockname(s, (struct sockaddr*)&a, &n) == 0)
				port = (int)ntohs(a.sin_port);
			close(s);
			return port;
		}

		std::string bundleDir()
		{
			std::string base;
			if (char* b = SDL_GetBasePath())
			{
				base = b;
				SDL_free(b);
			}
			while (!base.empty() && base.back() == '/') base.pop_back();
			if (base.size() > 4 && base.compare(base.size() - 4, 4, "/bin") == 0) base.resize(base.size() - 4);
			return base;
		}

		std::string lastLogLine(const std::string& path)
		{
			int fd = open(path.c_str(), O_RDONLY);
			if (fd < 0) return "";
			off_t end = lseek(fd, 0, SEEK_END);
			if (end <= 0)
			{
				close(fd);
				return "";
			}
			size_t n = (size_t)std::min<off_t>(end, 8192);
			std::string buf(n, '\0');
			ssize_t got = pread(fd, &buf[0], n, end - (off_t)n);
			close(fd);
			if (got <= 0) return "";
			buf.resize((size_t)got);
			while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r' || buf.back() == ' ' || buf.back() == '\t'))
				buf.pop_back();
			size_t p = buf.find_last_of('\n');
			std::string line = p == std::string::npos ? buf : buf.substr(p + 1);
			while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
			if (line.size() > 200)
			{
				// cut on a character: SDL_ttf gets this line too (half a UTF-8 sequence would draw as nothing)
				size_t cut = 200;
				while (cut > 0 && ((unsigned char)line[cut] & 0xC0) == 0x80) cut--;
				line.resize(cut);
				line += "...";
			}
			return line;
		}

		size_t discardBody(char*, size_t, size_t n, void*)
		{
			return n;
		}

		int httpStatus(const std::string& url)
		{
			static const bool curlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
			if (!curlReady) return 0;
			CURL* e = curl_easy_init();
			if (!e) return 0;
			char sink[256];
			curl_easy_setopt(e, CURLOPT_URL, url.c_str());
			curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
			curl_easy_setopt(e, CURLOPT_TIMEOUT_MS, 2500L);
			curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 0L);
#if LIBCURL_VERSION_NUM >= 0x075500
			curl_easy_setopt(e, CURLOPT_PROTOCOLS_STR, "http");
#else
			curl_easy_setopt(e, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTP);
#endif
			curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, &discardBody);
			curl_easy_setopt(e, CURLOPT_WRITEDATA, sink);
			CURLcode rc = curl_easy_perform(e);
			long code = 0;
			if (rc == CURLE_OK) curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &code);
			curl_easy_cleanup(e);
			return (int)code;
		}
	}

	// The host's state, shared by the main thread and its two detached workers (the installer, the process watcher):
	// the shared_ptr keeps it alive after a quit even if MangaNet were ever freed while a thread is still inside curl.
	struct JavaExtHost::Impl : std::enable_shared_from_this<JavaExtHost::Impl>
	{
		enum class Fetch { Ok, Cancelled, Failed };

		mutable std::mutex lock;
		std::mutex gate;	// one install job writes the folder at a time (Cancel, then Install again)
		JavaExtHost::State st = JavaExtHost::State::NotInstalled;
		std::string err;
		std::string javaDir;
		Uint32 wake = 0;
		int64_t done = 0, total = JavaExtPins::JRE_BYTES + JavaExtPins::SERVER_BYTES;
		int64_t installed = 0;
		uint64_t serial_ = 1;
		uint64_t job = 0;
		pid_t pid = -1;
		int port = 0;
		uint64_t procGen = 0;
		bool stopReq = false, idleStop = false, removing = false;
		int64_t lastWant = 0;
		std::condition_variable cv;

		// caller holds lock
		void set(JavaExtHost::State s, const std::string& why)
		{
			if (st == s && err == why) return;
			st = s;
			err = why;
			serial_++;
			fprintf(stderr, "javaext: %s %s%s%s\n", clockStamp().c_str(), stateName(s), why.empty() ? "" : ": ", why.c_str());
		}

		void pushWake()
		{
			if (!wake) return;
			SDL_Event ev {};
			ev.type = wake;
			SDL_PushEvent(&ev);
		}

		bool current(uint64_t j)
		{
			std::lock_guard<std::mutex> g(lock);
			return j == job;
		}

		bool javaBinary(std::string& out) const
		{
#ifdef __linux__
			out = javaDir + "/" + JRE_DIR + "/bin/java";
			return access(out.c_str(), X_OK) == 0;
#else
			// the pinned JRE is Linux/aarch64: a desktop build runs a Java of its own, named by BP_JAVA
			const char* env = getenv("BP_JAVA");
			if (!env || !*env) return false;
			out = env;
			return access(out.c_str(), X_OK) == 0;
#endif
		}

		// caller holds lock; a marker and both halves of the add-on
		bool cardInstalled(std::string* bin)
		{
			if (javaDir.empty()) return false;
			std::string java;
			if (!javaBinary(java)) return false;
			if (!isFile(javaDir + "/" + SERVER_JAR)) return false;
			int64_t bytes = 0;
			if (!readMarker(javaDir, bytes)) return false;
			if (bin) *bin = java;
			return true;
		}

		void setProgress(uint64_t j, int64_t bytes)
		{
			std::lock_guard<std::mutex> g(lock);
			if (j != job) return;
			done = total > 0 ? std::clamp<int64_t>(bytes, 0, total) : std::max<int64_t>(0, bytes);
			serial_++;
		}

		void failJob(uint64_t j, const std::string& why)
		{
			std::lock_guard<std::mutex> g(lock);
			if (j != job) return;
			set(JavaExtHost::State::Failed, why);
			pushWake();
		}

		void startLocked(const std::string& javaBin);
		void watch(pid_t child, int p, uint64_t gen);
		void installStep(uint64_t j);
		void removeStep();
		Fetch fetchFile(uint64_t j, const char* url, const char* wantSha, int64_t expected,
			const std::string& finalPath, int64_t& doneBase);

		struct FetchCtx
		{
			Impl* host = nullptr;
			uint64_t job = 0;
			int64_t base = 0;	// bytes of the files already whole
			int64_t resume = 0;	// bytes the .part held before this transfer
			int64_t lastWake = 0;
			FILE* fp = nullptr;
			bool cancelled = false;
		};

		static size_t onWrite(char* data, size_t size, size_t n, void* p)
		{
			FetchCtx* c = (FetchCtx*)p;
			return fwrite(data, size, n, c->fp);
		}

		static int onProgress(void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t dlnow)
		{
			FetchCtx* c = (FetchCtx*)p;
			Impl* d = c->host;
			std::lock_guard<std::mutex> g(d->lock);
			if (c->job != d->job)
			{
				c->cancelled = true;
				return 1;
			}
			int64_t value = c->base + c->resume + (int64_t)dlnow;
			d->done = d->total > 0 ? std::clamp<int64_t>(value, 0, d->total) : std::max<int64_t>(0, value);
			d->serial_++;
			int64_t now = sdlNow();
			if (now - c->lastWake >= 250)
			{
				c->lastWake = now;
				d->pushWake();
			}
			return 0;
		}
	};

	JavaExtHost::JavaExtHost() : impl(std::make_shared<Impl>()) {}
	JavaExtHost::~JavaExtHost() = default;

	void JavaExtHost::configure(Uint32 wakeEvent, const std::string& stateDir)
	{
		auto d = impl;
		std::lock_guard<std::mutex> g(d->lock);
		d->wake = wakeEvent;
		d->javaDir = stateDir.empty() ? "" : stateDir + "/manga/java";
		d->total = JavaExtPins::JRE_BYTES + JavaExtPins::SERVER_BYTES;
		if (d->cardInstalled(nullptr))
		{
			int64_t bytes = 0;
			if (!readMarker(d->javaDir, bytes) || bytes <= 0) bytes = treeSize(d->javaDir);
			d->installed = bytes;
			d->set(State::Installed, "");
		}
		else
		{
			d->installed = 0;
			d->set(State::NotInstalled, "");
		}
	}

	JavaExtHost::State JavaExtHost::state() const
	{
		std::lock_guard<std::mutex> g(impl->lock);
		return impl->st;
	}

	std::string JavaExtHost::error() const
	{
		std::lock_guard<std::mutex> g(impl->lock);
		return impl->err;
	}

	void JavaExtHost::progress(int64_t& done, int64_t& total) const
	{
		std::lock_guard<std::mutex> g(impl->lock);
		done = impl->done;
		total = impl->total;
	}

	int64_t JavaExtHost::installedBytes() const
	{
		std::lock_guard<std::mutex> g(impl->lock);
		return impl->installed;
	}

	void JavaExtHost::install()
	{
		auto d = impl;
		std::lock_guard<std::mutex> g(d->lock);
		if (d->javaDir.empty())
		{
			d->set(State::Failed, "No state folder");
			d->pushWake();
			return;
		}
		if (d->removing || d->st == State::Downloading || d->st == State::Starting || d->st == State::Running) return;
		if (d->cardInstalled(nullptr))
		{
			int64_t bytes = 0;
			if (!readMarker(d->javaDir, bytes) || bytes <= 0) bytes = treeSize(d->javaDir);
			d->installed = bytes;
			d->set(State::Installed, "");
			d->pushWake();
			return;
		}
		uint64_t j = ++d->job;
		d->total = JavaExtPins::JRE_BYTES + JavaExtPins::SERVER_BYTES;
		d->done = 0;
		d->installed = 0;
		d->set(State::Downloading, "");
		d->pushWake();
		fprintf(stderr, "javaext: %s download started (%lld B)\n", clockStamp().c_str(), (long long)d->total);
		auto self = d;
		std::thread([self, j] {
			std::lock_guard<std::mutex> gate(self->gate);
			self->installStep(j);
		}).detach();
	}

	void JavaExtHost::cancelInstall()
	{
		auto d = impl;
		std::lock_guard<std::mutex> g(d->lock);
		if (d->st != State::Downloading) return;
		++d->job;	// the running job is no longer current: its writes and its answers are dropped
		d->done = 0;
		d->set(State::NotInstalled, "");
		d->pushWake();
		fprintf(stderr, "javaext: %s download cancelled\n", clockStamp().c_str());
	}

	void JavaExtHost::remove()
	{
		auto d = impl;
		std::string dir;
		{
			std::lock_guard<std::mutex> g(d->lock);
			if (d->javaDir.empty()) return;
			dir = d->javaDir;
			++d->job;	// a download under way stops
			d->removing = true;
			d->stopReq = true;
			d->idleStop = false;
			d->installed = 0;
			d->done = 0;
			d->set(State::NotInstalled, "");
			d->cv.notify_all();
			d->pushWake();
		}
		fprintf(stderr, "javaext: %s remove: stopping it, then deleting %s\n", clockStamp().c_str(), dir.c_str());
		auto self = d;
		std::thread([self] { self->removeStep(); }).detach();
	}

	void JavaExtHost::want()
	{
		auto d = impl;
		std::lock_guard<std::mutex> g(d->lock);
		d->lastWant = sdlNow();
		if (d->removing) return;
		if (d->pid >= 0)
		{
			// a call is use: an idle stop not yet sent stands down (an explicit stop does not)
			if (!d->stopReq) d->idleStop = false;
			d->cv.notify_all();
			return;
		}
		std::string java;
		if (!d->cardInstalled(&java)) return;
		d->startLocked(java);
	}

	void JavaExtHost::pump(int64_t nowMs)
	{
		auto d = impl;
		std::lock_guard<std::mutex> g(d->lock);
		if (d->pid < 0 || (d->st != State::Starting && d->st != State::Running)) return;
		if (d->stopReq || d->idleStop) return;
		if (d->lastWant <= 0)
		{
			d->lastWant = nowMs;
			return;
		}
		if (nowMs - d->lastWant >= IDLE_STOP_MS)
		{
			d->idleStop = true;
			fprintf(stderr, "javaext: %s idle %lld s: stopping it\n", clockStamp().c_str(),
				(long long)((nowMs - d->lastWant) / 1000));
			d->cv.notify_all();
		}
	}

	void JavaExtHost::stop()
	{
		auto d = impl;
		std::lock_guard<std::mutex> g(d->lock);
		d->idleStop = false;
		if (d->pid >= 0 && !d->stopReq)
		{
			d->stopReq = true;
			fprintf(stderr, "javaext: %s stopping (extension off or quitting)\n", clockStamp().c_str());
		}
		d->cv.notify_all();
	}

	std::string JavaExtHost::baseUrl() const
	{
		std::lock_guard<std::mutex> g(impl->lock);
		if (impl->st != State::Running || impl->port <= 0) return "";
		return "http://127.0.0.1:" + std::to_string(impl->port);
	}

	uint64_t JavaExtHost::serial() const
	{
		std::lock_guard<std::mutex> g(impl->lock);
		return impl->serial_;
	}

	JavaExtHost::Impl::Fetch JavaExtHost::Impl::fetchFile(uint64_t j, const char* url, const char* wantSha,
		int64_t expected, const std::string& finalPath, int64_t& doneBase)
	{
		static const bool curlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
		if (!curlReady)
		{
			failJob(j, "libcurl couldn't start");
			return Fetch::Failed;
		}
		std::string part = finalPath + ".part";
		// a file from a run before: only its pinned digest makes it usable
		if (isFile(finalPath))
		{
			if (sha256File(finalPath) == wantSha)
			{
				doneBase += fileSize(finalPath);
				setProgress(j, doneBase);
				return Fetch::Ok;
			}
			unlink(finalPath.c_str());
		}
		int64_t from = fileSize(part);
		if (from > expected)
		{
			unlink(part.c_str());
			from = 0;
		}
		if (from == expected && from > 0)
		{
			if (sha256File(part) == wantSha && rename(part.c_str(), finalPath.c_str()) == 0)
			{
				doneBase += expected;
				setProgress(j, doneBase);
				return Fetch::Ok;
			}
			unlink(part.c_str());
			from = 0;
		}
		if (!current(j)) return Fetch::Cancelled;

		for (int attempt = 0; attempt < 2; attempt++)
		{
			bool resume = from > 0;
			FILE* fp = fopen(part.c_str(), resume ? "ab" : "wb");
			if (!fp)
			{
				failJob(j, "Couldn't write " + part);
				return Fetch::Failed;
			}
			FetchCtx ctx;
			ctx.host = this;
			ctx.job = j;
			ctx.base = doneBase;
			ctx.resume = from;
			ctx.lastWake = sdlNow();
			ctx.fp = fp;

			CURL* e = curl_easy_init();
			if (!e)
			{
				fclose(fp);
				failJob(j, "Couldn't make a request");
				return Fetch::Failed;
			}
			char errbuf[CURL_ERROR_SIZE] = {};
			curl_easy_setopt(e, CURLOPT_URL, url);
			curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(e, CURLOPT_MAXREDIRS, 10L);
#if LIBCURL_VERSION_NUM >= 0x075500
			curl_easy_setopt(e, CURLOPT_PROTOCOLS_STR, "https");
			curl_easy_setopt(e, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
			curl_easy_setopt(e, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
			curl_easy_setopt(e, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif
			// the download is a pinned binary: the certificate is checked both ways, as every other request here
			curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, 1L);
			curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, 2L);
			curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT, 20L);
			curl_easy_setopt(e, CURLOPT_LOW_SPEED_LIMIT, 1024L);
			curl_easy_setopt(e, CURLOPT_LOW_SPEED_TIME, 60L);
			curl_easy_setopt(e, CURLOPT_USERAGENT, "banana-player");
			curl_easy_setopt(e, CURLOPT_TCP_KEEPALIVE, 1L);
			curl_easy_setopt(e, CURLOPT_BUFFERSIZE, 256L * 1024);
			curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, &Impl::onWrite);
			curl_easy_setopt(e, CURLOPT_WRITEDATA, &ctx);
			curl_easy_setopt(e, CURLOPT_XFERINFOFUNCTION, &Impl::onProgress);
			curl_easy_setopt(e, CURLOPT_XFERINFODATA, &ctx);
			curl_easy_setopt(e, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(e, CURLOPT_ERRORBUFFER, errbuf);
			if (resume) curl_easy_setopt(e, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)from);
			CURLcode rc = curl_easy_perform(e);
			long code = 0;
			curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &code);
			curl_easy_cleanup(e);
			fclose(fp);
			if (ctx.cancelled || !current(j)) return Fetch::Cancelled;
			// the server wouldn't take the Range (a proxy, a changed reply): this file starts over
			if (rc == CURLE_RANGE_ERROR && resume)
			{
				unlink(part.c_str());
				from = 0;
				continue;
			}
			if (rc != CURLE_OK)
			{
				failJob(j, std::string("Download failed: ") + (errbuf[0] ? errbuf : curl_easy_strerror(rc)));
				return Fetch::Failed;
			}
			if (code != 200 && code != 206)
			{
				failJob(j, "The download answered " + std::to_string(code));
				return Fetch::Failed;
			}
			if (sha256File(part) != wantSha)
			{
				unlink(part.c_str());
				failJob(j, "The download isn't the pinned file (its SHA-256 doesn't match)");
				return Fetch::Failed;
			}
			if (rename(part.c_str(), finalPath.c_str()) != 0)
			{
				failJob(j, "Couldn't save " + finalPath);
				return Fetch::Failed;
			}
			doneBase += expected;
			setProgress(j, doneBase);
			return Fetch::Ok;
		}
		failJob(j, "The download couldn't resume");
		return Fetch::Failed;
	}

	void JavaExtHost::Impl::installStep(uint64_t j)
	{
		if (!current(j)) return;
		if (!ensureDir(javaDir))
		{
			failJob(j, "Couldn't make " + javaDir);
			return;
		}
		bool needJre = true;
#ifndef __linux__
		std::string ignored;
		needJre = !javaBinary(ignored);
		if (needJre)
		{
			failJob(j, "Set BP_JAVA to a Java runtime on this computer");
			return;
		}
#endif
		// a runtime unpacked by an attempt that stopped before the server was whole is whole itself: jre only exists
		// once the archive was unpacked into a stage and its one folder renamed in place
		bool getJre = needJre && !isFile(javaDir + "/" + JRE_DIR + "/bin/java");
		int64_t tot = (getJre ? JavaExtPins::JRE_BYTES : 0) + JavaExtPins::SERVER_BYTES;
		{
			std::lock_guard<std::mutex> g(lock);
			total = tot;
		}
		int64_t base = 0;
		if (getJre)
		{
			Fetch r = fetchFile(j, JavaExtPins::JRE_URL, JavaExtPins::JRE_SHA256, JavaExtPins::JRE_BYTES,
				javaDir + "/" + JRE_TAR, base);
			if (r != Fetch::Ok) return;
			// the archive's one top folder is the runtime: unpack into a stage, then put that folder in place
			std::string stage = javaDir + "/" + JRE_STAGE, dest = javaDir + "/" + JRE_DIR;
			removeTree(stage);
			removeTree(dest);
			std::string why;
			if (!extractTarGz(javaDir + "/" + JRE_TAR, stage, why))
			{
				removeTree(stage);
				failJob(j, "The Java runtime couldn't be unpacked: " + why);
				return;
			}
			auto entries = listDir(stage);
			bool moved = false;
			if (entries.size() == 1 && isDir(stage + "/" + entries[0]))
			{
				moved = rename((stage + "/" + entries[0]).c_str(), dest.c_str()) == 0;
				if (moved) rmdir(stage.c_str());
			}
			if (!moved) moved = rename(stage.c_str(), dest.c_str()) == 0;
			if (!moved || !isFile(dest + "/bin/java"))
			{
				removeTree(stage);
				removeTree(dest);
				failJob(j, "The Java runtime has no bin/java");
				return;
			}
			removeTree(stage);
			// the unpacked runtime is what stays on the card; the 51 MB archive is not needed again
			unlink((javaDir + "/" + JRE_TAR).c_str());
		}
		else if (needJre)
		{
			// a runtime unpacked before the server was whole: its archive and stage are not needed
			unlink((javaDir + "/" + JRE_TAR).c_str());
			removeTree(javaDir + "/" + JRE_STAGE);
		}
		if (!current(j)) return;
		Fetch r = fetchFile(j, JavaExtPins::SERVER_URL, JavaExtPins::SERVER_SHA256, JavaExtPins::SERVER_BYTES,
			javaDir + "/" + SERVER_JAR, base);
		if (r != Fetch::Ok) return;
		if (!current(j)) return;
		int64_t bytes = treeSize(javaDir);
		if (!writeMarker(javaDir, bytes))
		{
			failJob(j, "Couldn't write the installed marker");
			return;
		}
		std::lock_guard<std::mutex> g(lock);
		if (j != job) return;
		installed = bytes;
		done = total;
		set(State::Installed, "");
		pushWake();
		fprintf(stderr, "javaext: %s installed (%lld MB)\n", clockStamp().c_str(),
			(long long)((bytes + 500000) / 1000000));
	}

	void JavaExtHost::Impl::removeStep()
	{
		{
			std::unique_lock<std::mutex> lk(lock);
			cv.wait_for(lk, std::chrono::seconds(8), [this] { return pid < 0; });
		}
		{
			std::lock_guard<std::mutex> gate2(gate);	// a cancelled install leaving the folder
		}
		std::string dir = javaDir;
		bool ok = removeTree(dir);
		{
			std::lock_guard<std::mutex> g(lock);
			removing = false;
			installed = 0;
			done = 0;
			serial_++;
			pushWake();
		}
		fprintf(stderr, "javaext: %s %s %s\n", clockStamp().c_str(), ok ? "removed" : "couldn't remove", dir.c_str());
	}

	void JavaExtHost::Impl::startLocked(const std::string& java)
	{
		if (!ensureDir(javaDir))
		{
			set(State::Failed, "Couldn't make " + javaDir);
			pushWake();
			return;
		}
		std::string jar = javaDir + "/" + SERVER_JAR;
		if (!isFile(jar))
		{
			set(State::Failed, "The Java extensions' server jar is missing");
			pushWake();
			return;
		}
		int p = freePort();
		if (p <= 0)
		{
			set(State::Failed, "No free port on 127.0.0.1");
			pushWake();
			return;
		}
		std::string log = javaDir + "/" + SERVER_LOG;
		std::string cp = jar;
		std::string bd = bundleDir();
		if (!bd.empty() && isFile(bd + "/java/bp-dns.jar")) cp += ":" + bd + "/java/bp-dns.jar";

		std::vector<std::string> args = {java, "-Xmx256m", "-XX:+UseSerialGC", "-XX:TieredStopAtLevel=1",
			"-Djava.awt.headless=true",
			"-Dsuwayomi.tachidesk.config.server.rootDir=" + javaDir + "/data",
			"-Dsuwayomi.tachidesk.config.server.ip=127.0.0.1",
			"-Dsuwayomi.tachidesk.config.server.port=" + std::to_string(p),
			"-Dsuwayomi.tachidesk.config.server.webUIEnabled=false",
			"-Dsuwayomi.tachidesk.config.server.initialOpenInBrowserEnabled=false",
			"-Dsuwayomi.tachidesk.config.server.systemTrayEnabled=false",
			"-cp", cp, "suwayomi.tachidesk.MainKt"};
		std::vector<char*> av;
		for (auto& s : args) av.push_back(const_cast<char*>(s.c_str()));
		av.push_back(nullptr);

		posix_spawn_file_actions_t fa;
		posix_spawn_file_actions_init(&fa);
		posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
		posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
		posix_spawn_file_actions_adddup2(&fa, 1, 2);
		posix_spawnattr_t attr;
		posix_spawnattr_init(&attr);
		posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
		posix_spawnattr_setpgroup(&attr, 0);	// the server and its threads in a group of their own: one kill ends them
		pid_t child = -1;
		int rc = posix_spawn(&child, java.c_str(), &fa, &attr, av.data(), environ);
		posix_spawn_file_actions_destroy(&fa);
		posix_spawnattr_destroy(&attr);
		if (rc != 0)
		{
			set(State::Failed, std::string("Couldn't start Java: ") + strerror(rc));
			pushWake();
			return;
		}

		pid = child;
		port = p;
		++procGen;
		stopReq = idleStop = false;
		lastWant = sdlNow();
		set(State::Starting, "");
		pushWake();
		fprintf(stderr, "javaext: %s started pid %ld on 127.0.0.1:%d (log %s)\n", clockStamp().c_str(), (long)child, p,
			log.c_str());
		uint64_t gen = procGen;
		auto self = shared_from_this();
		std::thread([self, child, p, gen] { self->watch(child, p, gen); }).detach();
	}

	void JavaExtHost::Impl::watch(pid_t child, int p, uint64_t gen)
	{
		using clock = std::chrono::steady_clock;
		bool termSent = false, killed = false;
		clock::time_point termAt;
		clock::time_point nextPoll = clock::now();
		while (true)
		{
			bool stopNow = false;
			{
				std::lock_guard<std::mutex> g(lock);
				if (gen != procGen) return;
				stopNow = stopReq || idleStop;
			}
			if (stopNow && !termSent)
			{
				termSent = true;
				termAt = clock::now();
				if (kill(-child, SIGTERM) != 0) kill(child, SIGTERM);
				fprintf(stderr, "javaext: %s SIGTERM to pid %ld\n", clockStamp().c_str(), (long)child);
			}
			if (termSent && !killed && clock::now() - termAt >= std::chrono::seconds(3))
			{
				killed = true;
				if (kill(-child, SIGKILL) != 0) kill(child, SIGKILL);
				fprintf(stderr, "javaext: %s SIGKILL to pid %ld\n", clockStamp().c_str(), (long)child);
			}
			int status = 0;
			pid_t r = waitpid(child, &status, WNOHANG);
			if (r == child || (r < 0 && errno != EINTR)) break;
			if (!stopNow && !termSent)
			{
				bool starting = false;
				{
					std::lock_guard<std::mutex> g(lock);
					starting = gen == procGen && st == State::Starting;
				}
				if (starting && clock::now() >= nextPoll)
				{
					nextPoll = clock::now() + std::chrono::milliseconds(500);
					if (httpStatus("http://127.0.0.1:" + std::to_string(p) + "/api/v1/settings/about") == 200)
					{
						std::lock_guard<std::mutex> g(lock);
						if (gen == procGen && st == State::Starting)
						{
							set(State::Running, "");
							pushWake();
							fprintf(stderr, "javaext: %s answering on 127.0.0.1:%d\n", clockStamp().c_str(), p);
						}
					}
				}
			}
			{
				std::unique_lock<std::mutex> lk(lock);
				if (gen != procGen) return;
				cv.wait_for(lk, std::chrono::milliseconds(250));
			}
		}

		std::string last = lastLogLine(javaDir + "/" + SERVER_LOG);
		std::lock_guard<std::mutex> g(lock);
		if (gen != procGen) return;
		bool stopped = stopReq || idleStop || removing;
		pid = -1;
		port = 0;
		stopReq = idleStop = false;
		if (stopped) set(removing ? State::NotInstalled : State::Installed, "");
		else set(State::Failed, last.empty() ? "the Java process ended" : last);
		pushWake();
		cv.notify_all();
	}
}
