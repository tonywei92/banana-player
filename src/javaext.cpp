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
		// never "jre": Suwayomi's class scanner (ClassGraph) takes a java.home ending in /jre to mean its parent is
		// the JDK too, and skips every jar under it — server.jar included, so the server dies at start
		// ("configured packages do not contain any valid classes"). Found on the handheld; the Mac's system Java
		// lives elsewhere and never showed it.
		constexpr const char* JRE_DIR = "runtime";
		constexpr const char* JRE_OLD_DIR = "jre";	// 0.6.4 test builds: moved to JRE_DIR at configure
		constexpr const char* JRE_STAGE = "runtime.tmp";
		constexpr const char* SERVER_LOG = "server.log";
		// the running server's pid, while it runs: a player that crashed (or was killed) never stopped it, and the next
		// start ends it (leftoverStop)
		constexpr const char* PID_FILE = "server.pid";

		// A server left running by a player that didn't get to stop it: its pid from PID_FILE, ended only when that
		// process is still ours (its command line names our folder, so a pid the system gave to something else since
		// is left alone). SIGTERM now, SIGKILL 3 s later from a thread of its own (the start never waits on it).
		// /proc is Linux's: elsewhere (the desktop) the file is only removed.
		void leftoverStop(const std::string& javaDir)
		{
			std::string file = javaDir + "/" + PID_FILE;
			FILE* f = fopen(file.c_str(), "r");
			if (!f) return;
			long pid = 0;
			if (fscanf(f, "%ld", &pid) != 1) pid = 0;
			fclose(f);
			unlink(file.c_str());
	#ifdef __linux__
			if (pid <= 1) return;
			auto ours = [javaDir](long p) {
				std::ifstream in("/proc/" + std::to_string(p) + "/cmdline", std::ios::binary);
				std::string cmd((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				return !cmd.empty() && cmd.find(javaDir + "/") != std::string::npos;
			};
			if (!ours(pid)) return;
			fprintf(stderr, "javaext: a server left running by the last run (pid %ld): stopping it\n", pid);
			if (kill(-(pid_t)pid, SIGTERM) != 0) kill((pid_t)pid, SIGTERM);
			std::thread([pid, ours] {
				std::this_thread::sleep_for(std::chrono::seconds(3));
				if (ours(pid) && (kill(-(pid_t)pid, SIGKILL) == 0 || kill((pid_t)pid, SIGKILL) == 0))
					fprintf(stderr, "javaext: the last run's server (pid %ld) didn't stop: SIGKILL\n", pid);
			}).detach();
	#else
			(void)pid;
	#endif
		}

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
		std::atomic<bool> secureDns{false};	// the Manga setting, read when the process starts
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

		// the GraphQL channel: its own mutex (the request thread must not hold the state lock while curl runs)
		struct GqlJob { uint64_t serial = 0; std::string body; };
		std::mutex gqlLock;
		std::condition_variable gqlCv;
		std::vector<GqlJob> gqlQueue;
		std::vector<JavaExtHost::GraphQlResult> gqlDone;
		uint64_t gqlNext = 1, gqlActive = 0;
		bool gqlActiveCancelled = false, gqlThreadStarted = false, gqlWakePending = false;

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
		void gqlLoop();
		void gqlFinish(uint64_t serial, int status, std::string body, std::string error);
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

		struct GqlCtx
		{
			Impl* host = nullptr;
			uint64_t serial = 0;
			std::string* body = nullptr;
			bool over = false;
		};

		static size_t gqlWrite(char* data, size_t size, size_t n, void* p);
		static int gqlProgress(void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t);

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
		if (!d->javaDir.empty())
		{
			std::string oldDir = d->javaDir + "/" + JRE_OLD_DIR, newDir = d->javaDir + "/" + JRE_DIR;
			if (isFile(oldDir + "/bin/java") && !isFile(newDir + "/bin/java") && rename(oldDir.c_str(), newDir.c_str()) == 0)
				fprintf(stderr, "javaext: moved the runtime from %s to %s\n", JRE_OLD_DIR, JRE_DIR);
			leftoverStop(d->javaDir);
		}
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

	bool JavaExtHost::stopping() const
	{
		std::lock_guard<std::mutex> g(impl->lock);
		return impl->pid >= 0 && (impl->stopReq || impl->idleStop);
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
		{
			std::lock_guard<std::mutex> g(d->lock);
			d->idleStop = false;
			if (d->pid >= 0 && !d->stopReq)
			{
				d->stopReq = true;
				fprintf(stderr, "javaext: %s stopping (extension off or quitting)\n", clockStamp().c_str());
				// the signal now, not only by the watcher: quitting, the player may be gone before the watcher wakes
				// (it can be inside a start poll), and the server would live on with its 400 MB (a kill doesn't wait)
				if (kill(-d->pid, SIGTERM) != 0) kill(d->pid, SIGTERM);
			}
			d->cv.notify_all();
		}
		// a request in flight would only fail against the port that is going away; drop it and anything queued
		std::lock_guard<std::mutex> g(d->gqlLock);
		d->gqlActiveCancelled = true;
		d->gqlQueue.clear();
		d->gqlCv.notify_all();
	}

	uint64_t JavaExtHost::graphql(const std::string& requestBody)
	{
		auto d = impl;
		{
			std::lock_guard<std::mutex> g(d->lock);
			if (!d->cardInstalled(nullptr)) return 0;
		}
		want();	// every request is use
		std::lock_guard<std::mutex> g(d->gqlLock);
		if (!d->gqlThreadStarted)
		{
			d->gqlThreadStarted = true;
			auto self = d;
			std::thread([self] { self->gqlLoop(); }).detach();
		}
		uint64_t serial = d->gqlNext++;
		d->gqlQueue.push_back({serial, requestBody});
		d->gqlCv.notify_all();
		return serial;
	}

	void JavaExtHost::cancelGraphQl(uint64_t serial)
	{
		auto d = impl;
		std::lock_guard<std::mutex> g(d->gqlLock);
		for (auto it = d->gqlQueue.begin(); it != d->gqlQueue.end(); ++it)
			if (it->serial == serial)
			{
				d->gqlQueue.erase(it);
				return;
			}
		// an answer finished but not taken yet is still the caller's to forget
		for (auto it = d->gqlDone.begin(); it != d->gqlDone.end(); ++it)
			if (it->serial == serial)
			{
				d->gqlDone.erase(it);
				return;
			}
		if (d->gqlActive == serial) d->gqlActiveCancelled = true;
	}

	std::vector<JavaExtHost::GraphQlResult> JavaExtHost::takeGraphQl()
	{
		auto d = impl;
		std::lock_guard<std::mutex> g(d->gqlLock);
		d->gqlWakePending = false;
		std::vector<GraphQlResult> out;
		out.swap(d->gqlDone);
		return out;
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
			failJob(j, "Couldn't start the download");
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
				failJob(j, "Couldn't write to the card");
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
				failJob(j, "The download is damaged: try again");
				return Fetch::Failed;
			}
			if (rename(part.c_str(), finalPath.c_str()) != 0)
			{
				failJob(j, "Couldn't write to the card");
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
			failJob(j, "Couldn't write to the card");
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
				failJob(j, "The download couldn't be unpacked: " + why);
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
				failJob(j, "The download is incomplete");
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
			failJob(j, "Couldn't write to the card");
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
			set(State::Failed, "Couldn't write to the card");
			pushWake();
			return;
		}
		std::string jar = javaDir + "/" + SERVER_JAR;
		if (!isFile(jar))
		{
			set(State::Failed, "Some required files are missing: install again");
			pushWake();
			return;
		}
		int p = freePort();
		if (p <= 0)
		{
			set(State::Failed, "Couldn't start: no free port");
			pushWake();
			return;
		}
		std::string log = javaDir + "/" + SERVER_LOG;
		std::string cp = jar;
		std::string bd = bundleDir();
		if (!bd.empty() && isFile(bd + "/java/bp-dns.jar")) cp += ":" + bd + "/java/bp-dns.jar";

		// The runtime's own options. An OpenJ9 runtime (IBM Semeru: lib/default/libj9gc29.so, which HotSpot hasn't) uses
		// less memory for the same server; its class cache on the card makes later starts quicker. Trying it out on
		// the handheld (2026-11-12): the engine at idle 290-300 MB where HotSpot holds 366-374 MB
		auto parent = [](const std::string& path) { size_t at = path.rfind('/'); return at == std::string::npos ? std::string(".") : path.substr(0, at); };
		const bool openj9 = isFile(parent(parent(java)) + "/lib/default/libj9gc29.so");
		std::vector<std::string> args = {java, "-Xmx256m"};
		if (openj9)
		{
			// -Xquickstart: lighter early compiling (it started in 37 s and answered its first requests slowly; the
			// server mostly waits on the network); -Xms8m: the heap starts small and grows as it's used
			args.push_back("-Xquickstart");
			args.push_back("-Xms8m");
			args.push_back("-Xshareclasses:name=bp,cacheDir=" + javaDir + "/classcache");
			args.push_back("-Xtune:virtualized");
		}
		else
		{
			args.push_back("-XX:+UseSerialGC");
			args.push_back("-XX:TieredStopAtLevel=1");
		}
		std::vector<std::string> rest = {"-Djava.awt.headless=true",
			"-Dsuwayomi.tachidesk.config.server.rootDir=" + javaDir + "/data",
			"-Dsuwayomi.tachidesk.config.server.ip=127.0.0.1",
			"-Dsuwayomi.tachidesk.config.server.port=" + std::to_string(p),
			"-Dsuwayomi.tachidesk.config.server.webUIEnabled=false",
			"-Dsuwayomi.tachidesk.config.server.initialOpenInBrowserEnabled=false",
			"-Dsuwayomi.tachidesk.config.server.systemTrayEnabled=false",
			"-cp", cp, "suwayomi.tachidesk.MainKt"};
		args.insert(args.end(), rest.begin(), rest.end());
		fprintf(stderr, "javaext: runtime %s\n", openj9 ? "OpenJ9" : "HotSpot");
		// Secure DNS: bp-dns.jar's resolver asks Cloudflare over HTTPS for every name the extensions look up (read at
		// start: a change of the setting reaches the next start)
		if (secureDns.load() && cp.find("bp-dns.jar") != std::string::npos)
			args.insert(args.begin() + 1, "-Dbp.doh=https://1.1.1.1/dns-query");
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
			set(State::Failed, std::string("Couldn't start: ") + strerror(rc));
			pushWake();
			return;
		}

		pid = child;
		port = p;
		++procGen;
		// its pid on the card while it runs, for the next start to end it if this player never gets to (a crash)
		if (FILE* pf = fopen((javaDir + "/" + PID_FILE).c_str(), "w"))
		{
			fprintf(pf, "%ld\n", (long)child);
			fclose(pf);
		}
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
		// a server that's up but never answers (it failed inside, as with the scanner above) is ended after this,
		// so a request waiting on it fails instead of the screen saying "Starting" for good
		const clock::time_point startDeadline = clock::now() + std::chrono::seconds(150);
		bool timedOut = false;
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
				if (starting && clock::now() >= startDeadline)
				{
					timedOut = termSent = true;
					termAt = clock::now();
					if (kill(-child, SIGTERM) != 0) kill(child, SIGTERM);
					fprintf(stderr, "javaext: %s no answer in 150 s: SIGTERM to pid %ld\n", clockStamp().c_str(), (long)child);
				}
				else if (starting && clock::now() >= nextPoll)
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

		unlink((javaDir + "/" + PID_FILE).c_str());	// it ended: nothing for the next start to stop
		std::string last = lastLogLine(javaDir + "/" + SERVER_LOG);
		std::lock_guard<std::mutex> g(lock);
		if (gen != procGen) return;
		bool stopped = stopReq || idleStop || removing;
		pid = -1;
		port = 0;
		stopReq = idleStop = false;
		if (stopped) set(removing ? State::NotInstalled : State::Installed, "");
		else if (timedOut)
		{
			if (!last.empty()) fprintf(stderr, "javaext: %s last line: %s\n", clockStamp().c_str(), last.c_str());
			set(State::Failed, "it didn't start");
		}
		else
		{
			// the server's own last line is for log.txt; the screen says it plainly
			if (!last.empty()) fprintf(stderr, "javaext: %s ended: %s\n", clockStamp().c_str(), last.c_str());
			set(State::Failed, "it stopped unexpectedly");
		}
		pushWake();
		cv.notify_all();
	}

	size_t JavaExtHost::Impl::gqlWrite(char* data, size_t size, size_t n, void* p)
	{
		GqlCtx* c = (GqlCtx*)p;
		size_t bytes = size * n;
		// an extension list is large; anything past this isn't an answer this reader can use
		if (!c->body || c->body->size() + bytes > (32u << 20))
		{
			c->over = true;
			return 0;
		}
		c->body->append(data, bytes);
		return bytes;
	}

	int JavaExtHost::Impl::gqlProgress(void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
	{
		GqlCtx* c = (GqlCtx*)p;
		std::lock_guard<std::mutex> g(c->host->gqlLock);
		if (c->host->gqlActive != c->serial || c->host->gqlActiveCancelled) return 1;
		return 0;
	}

	void JavaExtHost::Impl::gqlFinish(uint64_t serial, int status, std::string body, std::string error)
	{
		bool signal = false;
		// one line per request: a list that never fills is pinned to an unanswered or failed call
		fprintf(stderr, "javaext: %s graphql #%llu -> %d, %zu bytes%s%s\n", clockStamp().c_str(),
			(unsigned long long)serial, status, body.size(), error.empty() ? "" : ": ", error.c_str());
		// an answer that is GraphQL's errors: its first lines in the log (the screen shows only the start of one)
		if (body.size() < 4096 && body.find("\"errors\"") != std::string::npos)
			fprintf(stderr, "javaext: graphql #%llu errors: %.600s\n", (unsigned long long)serial, body.c_str());
		{
			std::lock_guard<std::mutex> g(gqlLock);
			bool wasActive = gqlActive == serial;
			bool cancelled = wasActive && gqlActiveCancelled;
			if (wasActive) gqlActive = 0;
			if (wasActive) gqlActiveCancelled = false;
			if (!cancelled)
			{
				JavaExtHost::GraphQlResult r;
				r.serial = serial;
				r.status = status;
				r.body = std::move(body);
				r.error = std::move(error);
				gqlDone.push_back(std::move(r));
				if (!gqlWakePending)
				{
					gqlWakePending = true;
					signal = true;
				}
			}
		}
		if (signal) pushWake();
	}

	void JavaExtHost::Impl::gqlLoop()
	{
		while (true)
		{
			GqlJob job;
			{
				std::unique_lock<std::mutex> lk(gqlLock);
				gqlCv.wait(lk, [this] { return !gqlQueue.empty(); });
				job = std::move(gqlQueue.front());
				gqlQueue.erase(gqlQueue.begin());
				gqlActive = job.serial;
				gqlActiveCancelled = false;
			}

			// The UI may ask the moment a Java source opens, while the server is still Starting: wait for it (the
			// idle stop never starts while a request is waiting, since graphql() called want()).
			std::string base, why;
			bool cancelled = false;
			auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
			while (true)
			{
				{
					std::lock_guard<std::mutex> g(lock);
					if (st == State::Running && port > 0) base = "http://127.0.0.1:" + std::to_string(port);
					else if (st == State::NotInstalled) why = "extension support isn't installed";
					else if (st == State::Downloading) why = "extension support is still downloading";
					else if (st == State::Failed) why = err.empty() ? "the manga engine didn't start" : err;
				}
				if (!base.empty() || !why.empty()) break;
				if (std::chrono::steady_clock::now() >= deadline)
				{
					why = "the manga engine didn't start";
					break;
				}
				std::unique_lock<std::mutex> lk(gqlLock);
				if (gqlActive != job.serial || gqlActiveCancelled) { cancelled = true; break; }
				gqlCv.wait_for(lk, std::chrono::milliseconds(100));
			}
			if (cancelled)
			{
				gqlFinish(job.serial, 0, "", "");
				continue;
			}
			if (base.empty())
			{
				gqlFinish(job.serial, 0, "", why);
				continue;
			}

			static const bool curlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
			if (!curlReady)
			{
				gqlFinish(job.serial, 0, "", "libcurl couldn't start");
				continue;
			}
			CURL* e = curl_easy_init();
			if (!e)
			{
				gqlFinish(job.serial, 0, "", "Couldn't make a request");
				continue;
			}
			std::string out;
			GqlCtx ctx;
			ctx.host = this;
			ctx.serial = job.serial;
			ctx.body = &out;
			std::string url = base + "/api/graphql";
			struct curl_slist* headers = nullptr;
			headers = curl_slist_append(headers, "Content-Type: application/json");
			headers = curl_slist_append(headers, "Accept: application/json");
			char errbuf[CURL_ERROR_SIZE] = {};
			curl_easy_setopt(e, CURLOPT_URL, url.c_str());
			curl_easy_setopt(e, CURLOPT_POST, 1L);
			curl_easy_setopt(e, CURLOPT_POSTFIELDS, job.body.data());
			curl_easy_setopt(e, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)job.body.size());
			curl_easy_setopt(e, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT, 5L);
			curl_easy_setopt(e, CURLOPT_TIMEOUT, 120L);
			curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 0L);
#if LIBCURL_VERSION_NUM >= 0x075500
			curl_easy_setopt(e, CURLOPT_PROTOCOLS_STR, "http");
#else
			curl_easy_setopt(e, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTP);
#endif
			curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, &Impl::gqlWrite);
			curl_easy_setopt(e, CURLOPT_WRITEDATA, &ctx);
			curl_easy_setopt(e, CURLOPT_XFERINFOFUNCTION, &Impl::gqlProgress);
			curl_easy_setopt(e, CURLOPT_XFERINFODATA, &ctx);
			curl_easy_setopt(e, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(e, CURLOPT_ERRORBUFFER, errbuf);
			CURLcode rc = curl_easy_perform(e);
			long code = 0;
			curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &code);
			curl_easy_cleanup(e);
			curl_slist_free_all(headers);
			if (ctx.over)
			{
				gqlFinish(job.serial, (int)code, "", "the answer is over 32 MB");
				continue;
			}
			if (rc != CURLE_OK)
			{
				// an abort caused by cancelGraphQl() is dropped by gqlFinish, not reported as a failure
				gqlFinish(job.serial, (int)code, "", errbuf[0] ? errbuf : curl_easy_strerror(rc));
				continue;
			}
			if (code != 200)
			{
				gqlFinish(job.serial, (int)code, "", "the extensions answered " + std::to_string(code));
				continue;
			}
			gqlFinish(job.serial, (int)code, std::move(out), "");
		}
	}

	void JavaExtHost::setSecureDns(bool on)
	{
		impl->secureDns = on;
	}
}
