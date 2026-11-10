#include "mangaworker.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <curl/curl.h>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		constexpr size_t kMaxBody = 8u * 1024u * 1024u;	// a script's request (5.4); images are fetched elsewhere
		constexpr int kDefaultTimeout = 20;
		constexpr int kMaxTimeout = 30;
		constexpr long kConnectMs = 15000;
		constexpr long kMaxRedirects = 5;
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

		struct Easy
		{
			CURL* e = nullptr;
			curl_slist* list = nullptr;
			~Easy()
			{
				// the handle goes first: the header list is not owned by curl, but its cleanup must not see it gone
				if (e) curl_easy_cleanup(e);
				if (list) curl_slist_free_all(list);
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
			int timeout = timeoutSeconds <= 0 ? kDefaultTimeout : timeoutSeconds;
			if (timeout > kMaxTimeout) timeout = kMaxTimeout;

			Easy easy;
			easy.e = curl_easy_init();
			if (!easy.e) throw std::runtime_error("couldn't make a request");
			CURL* e = easy.e;
			char errbuf[CURL_ERROR_SIZE] = {};
			Sink sink;
			curl_easy_setopt(e, CURLOPT_URL, url.c_str());
			curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(e, CURLOPT_MAXREDIRS, kMaxRedirects);
#if LIBCURL_VERSION_NUM >= 0x075500	// 7.85.0: the string forms of the protocol masks
			curl_easy_setopt(e, CURLOPT_PROTOCOLS_STR, "http,https");
			curl_easy_setopt(e, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
			curl_easy_setopt(e, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
			curl_easy_setopt(e, CURLOPT_REDIR_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
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
			// The design's User-Agent is the player's version, unless the source brings its own: its headers reach
			// this list already (the host merges meta.headers with the call's), so only the default is set here.
			bool ownAgent = false;
			for (const auto& h : headers)
				if (iequals(h.first, "User-Agent")) ownAgent = true;
			if (!ownAgent) curl_easy_setopt(e, CURLOPT_USERAGENT, userAgent.c_str());
			if (!easy.addHeaders(headers)) throw std::runtime_error("out of memory");
			if (easy.list) curl_easy_setopt(e, CURLOPT_HTTPHEADER, easy.list);
			if (method == "POST")
			{
				curl_easy_setopt(e, CURLOPT_POST, 1L);
				curl_easy_setopt(e, CURLOPT_POSTFIELDS, body.data());
				curl_easy_setopt(e, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body.size());
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

			HttpResult r;
			long status = 0;
			if (curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK) r.status = (int)status;
			char* finalUrl = nullptr;
			if (curl_easy_getinfo(e, CURLINFO_EFFECTIVE_URL, &finalUrl) == CURLE_OK && finalUrl) r.url = finalUrl;
			r.body.swap(sink.body);
			r.headers.swap(sink.headers);
			return r;
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
		std::atomic<bool> abortCurrent{false};
		std::atomic<int> openCount{0};
		std::thread thread;
		bool started = false, stopping = false, eventPending = false, dropCurrent = false;

		~Impl()
		{
			for (auto& kv : cookies)
				if (kv.second) curl_share_cleanup(kv.second);
		}

		void run();
		Result runJob(const Job& job, std::map<std::string, std::shared_ptr<SourceState>>& open);
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

	MangaWorker::Result MangaWorker::Impl::runJob(const Job& job,
		std::map<std::string, std::shared_ptr<SourceState>>& open)
	{
		Result out;
		out.kind = job.kind;
		out.sourceId = job.sourceId;
		try
		{
			std::shared_ptr<SourceState> st = openSource(job, open, out.error);
			if (!st) return out;
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
			}
			// A call happened, so the 5 minutes count from here even when it failed: the state itself is still good.
			st->lastUse = Clock::now();
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
		// queue ten Popular pages and a slow source doesn't lose its turn to them.
		for (auto it = d->queue.begin(); it != d->queue.end(); ++it)
		{
			if (it->job.sourceId == job.sourceId && it->job.kind == job.kind)
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
