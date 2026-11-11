#include "mangafetch.h"
#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <curl/curl.h>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace bp
{
	namespace
	{
		constexpr size_t kMaxActive = 4;
		constexpr int kMaxPerHost = 2;
		constexpr long kConnectMs = 15000;
		constexpr long kLowSpeedBytes = 1024;	// under this for 30 s: the server went away, not a slow page
		constexpr long kLowSpeedSecs = 30;
		constexpr long kMaxRedirects = 5;
		constexpr size_t kBodyProbe = 4096;	// enough of the body for a browser check's markers, never the whole page

		// "https://user@host:8443/a?b" -> "host", so one server's two slots are shared by every scheme, port and path
		// of it (the host is what the cap is about, not the URL)
		std::string hostOf(const std::string& url)
		{
			size_t scheme = url.find("://");
			size_t i = scheme == std::string::npos ? 0 : scheme + 3;
			size_t end = url.find_first_of("/?#", i);
			std::string authority = url.substr(i, end == std::string::npos ? std::string::npos : end - i);
			size_t at = authority.rfind('@');
			if (at != std::string::npos) authority.erase(0, at + 1);
			std::string host;
			if (!authority.empty() && authority.front() == '[')
			{
				size_t close = authority.find(']');
				host = authority.substr(0, close == std::string::npos ? authority.size() : close + 1);
			}
			else
			{
				host = authority.substr(0, authority.find(':'));
			}
			std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return host;
		}

		std::string trim(const std::string& s)
		{
			size_t a = 0, b = s.size();
			while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
			while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
			return s.substr(a, b - a);
		}
	}

	struct MangaFetch::Impl
	{
		struct Request
		{
			uint64_t id = 0;
			std::string url, host, toFile, part;
			Headers headers;
			Headers responseHeaders;	// the answer's headers, kept for a browser check's own
			std::string bodyHead;		// its first bytes: a toFile body is not kept in memory
			int64_t maxBytes = 0, received = 0;
			std::vector<uint8_t> body;
			FILE* fp = nullptr;
			std::string error;			// the reason this request must fail (the write callback's own)
			bool over = false;			// the body grew past maxBytes
			CURL* easy = nullptr;
			curl_slist* http = nullptr;
			char errbuf[CURL_ERROR_SIZE] = {};
		};

		// the body as it arrives; 0 aborts the transfer (over the cap, or the file refused it)
		static size_t onWrite(char* data, size_t size, size_t nmemb, void* user)
		{
			Request* r = (Request*)user;
			size_t n = size * nmemb;
			if (r->maxBytes > 0 && r->received + (int64_t)n > r->maxBytes)
			{
				r->over = true;
				return 0;
			}
			// The head is kept even when the body goes straight to a file: a challenge's page is all markers.
			if (r->bodyHead.size() < kBodyProbe)
				r->bodyHead.append(data, std::min(n, kBodyProbe - r->bodyHead.size()));
			if (r->fp)
			{
				if (fwrite(data, 1, n, r->fp) != n)
				{
					r->error = "couldn't write " + r->part;
					return 0;
				}
			}
			else
			{
				const uint8_t* p = (const uint8_t*)data;
				r->body.insert(r->body.end(), p, p + n);
			}
			r->received += (int64_t)n;
			return n;
		}

		// The answer's headers, in the order they came; a redirect chain's earlier hops come too, and a lookup takes
		// the last value, so the final answer wins.
		static size_t onHeader(char* data, size_t size, size_t nmemb, void* user)
		{
			Request* r = (Request*)user;
			size_t n = size * nmemb;
			std::string line(data, n);
			size_t colon = line.find(':');
			if (colon != std::string::npos)
			{
				std::string name = trim(line.substr(0, colon));
				std::string value = trim(line.substr(colon + 1));
				if (!name.empty()) r->responseHeaders.emplace_back(name, value);
			}
			return n;
		}

		// curl's own options for one transfer: a page is a plain GET, but a source may need a Referer or an Accept
		bool prepare(Request& r)
		{
			r.easy = curl_easy_init();
			if (!r.easy)
			{
				r.error = "couldn't make a request";
				return false;
			}
			if (!r.toFile.empty())
			{
				r.fp = fopen(r.part.c_str(), "wb");
				if (!r.fp)
				{
					r.error = "couldn't write " + r.part;
					return false;
				}
			}
			CURL* e = r.easy;
			curl_easy_setopt(e, CURLOPT_URL, r.url.c_str());
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
			curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT_MS, kConnectMs);
			curl_easy_setopt(e, CURLOPT_LOW_SPEED_LIMIT, kLowSpeedBytes);
			curl_easy_setopt(e, CURLOPT_LOW_SPEED_TIME, kLowSpeedSecs);
			curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, 1L);
			curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, 2L);
			curl_easy_setopt(e, CURLOPT_USERAGENT, userAgent.c_str());
			// no cookie jar: nothing is stored, and every request starts clean (as a reader wants)
			curl_easy_setopt(e, CURLOPT_ACCEPT_ENCODING, "");
			curl_easy_setopt(e, CURLOPT_TCP_KEEPALIVE, 1L);
			curl_easy_setopt(e, CURLOPT_BUFFERSIZE, 256L * 1024);
			curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, &Impl::onWrite);
			curl_easy_setopt(e, CURLOPT_WRITEDATA, &r);
			curl_easy_setopt(e, CURLOPT_HEADERFUNCTION, &Impl::onHeader);
			curl_easy_setopt(e, CURLOPT_HEADERDATA, &r);
			curl_easy_setopt(e, CURLOPT_ERRORBUFFER, r.errbuf);
			if (!r.headers.empty())
			{
				curl_slist* list = nullptr;
				for (auto& h : r.headers)
				{
					if (h.first.empty()) continue;
					std::string line = h.first + ": " + h.second;
					curl_slist* next = curl_slist_append(list, line.c_str());
					if (!next)
					{
						if (list) curl_slist_free_all(list);
						r.error = "out of memory";
						return false;
					}
					list = next;
				}
				r.http = list;
				if (r.http) curl_easy_setopt(e, CURLOPT_HTTPHEADER, r.http);
			}
			return true;
		}

		// close a transfer that is going no further (aborted, or a quit): the part file is never left looking whole
		void discard(Request& r)
		{
			if (r.fp)
			{
				fclose(r.fp);
				r.fp = nullptr;
			}
			if (!r.part.empty()) std::remove(r.part.c_str());
			if (r.easy)
			{
				curl_easy_cleanup(r.easy);
				r.easy = nullptr;
			}
			if (r.http)
			{
				curl_slist_free_all(r.http);
				r.http = nullptr;
			}
		}

		// the transfer is over (or never started): read curl's answer, put it where take() finds it
		void finish(std::unique_ptr<Request> r, CURLcode code)
		{
			Result out;
			out.id = r->id;
			if (r->easy)
			{
				long status = 0;
				if (curl_easy_getinfo(r->easy, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK) out.status = (int)status;
				char* text = nullptr;
				if (curl_easy_getinfo(r->easy, CURLINFO_CONTENT_TYPE, &text) == CURLE_OK && text) out.contentType = text;
				if (curl_easy_getinfo(r->easy, CURLINFO_EFFECTIVE_URL, &text) == CURLE_OK && text) out.finalUrl = text;
			}
			out.headers = std::move(r->responseHeaders);
			out.bodyStart = std::move(r->bodyHead);
			if (r->fp)
			{
				if (fclose(r->fp) != 0 && r->error.empty()) r->error = "couldn't write " + r->part;
				r->fp = nullptr;
			}
			if (code != CURLE_OK)
			{
				if (r->over) out.error = "the response is over " + std::to_string(r->maxBytes) + " bytes";
				else if (!r->error.empty()) out.error = r->error;
				else if (r->errbuf[0]) out.error = r->errbuf;
				else out.error = curl_easy_strerror(code);
			}
			else if (out.status >= 300)
			{
				// an answer that isn't the page: the number is the useful part, and it keeps a 404's HTML from
				// becoming a cached picture
				out.error = "HTTP " + std::to_string(out.status);
			}
			if (!r->toFile.empty())
			{
				if (out.error.empty())
				{
					if (std::rename(r->part.c_str(), r->toFile.c_str()) == 0) out.file = r->toFile;
					else out.error = "couldn't rename " + r->part;
				}
				if (out.file.empty()) std::remove(r->part.c_str());
			}
			else if (out.error.empty())
			{
				out.body.swap(r->body);
			}
			if (r->easy)
			{
				curl_easy_cleanup(r->easy);
				r->easy = nullptr;
			}
			if (r->http)
			{
				curl_slist_free_all(r->http);
				r->http = nullptr;
			}
			bool signal = false;
			{
				std::lock_guard<std::mutex> g(m);
				bool wanted = !stopping && !cancelAllWanted;
				if (wanted && cancelWanted.find(out.id) != cancelWanted.end())
				{
					cancelWanted.erase(out.id);
					wanted = false;
				}
				if (wanted)
				{
					results.push_back(std::move(out));
					if (!eventPending)
					{
						eventPending = true;
						signal = true;
					}
				}
			}
			if (signal) pushEvent();
		}

		void pushEvent()
		{
			if (!wakeEvent) return;
			SDL_Event ev{};
			ev.type = wakeEvent;
			// a full queue means the loop is behind anyway; not losing the next wake matters more than this one
			if (SDL_PushEvent(&ev) < 0)
			{
				std::lock_guard<std::mutex> g(m);
				eventPending = false;
			}
		}

		void wakeLocked()
		{
#if LIBCURL_VERSION_NUM >= 0x074400	// 7.68.0: curl_multi_wakeup
			if (multiAlive && multi) curl_multi_wakeup(multi);
#endif
			cv.notify_one();
		}

		void run()
		{
			CURLM* mh = curl_multi_init();
			{
				std::lock_guard<std::mutex> g(m);
				multi = mh;
				multiAlive = mh != nullptr;
				if (!mh) stopping = true;
			}
			std::vector<std::unique_ptr<Request>> active;
			std::map<std::string, int> perHost;

			while (mh)
			{
				std::vector<std::unique_ptr<Request>> starts;
				std::set<uint64_t> aborts;
				bool all = false, leaving = false;
				{
					std::lock_guard<std::mutex> g(m);
					leaving = stopping;
					if (!leaving)
					{
						all = cancelAllWanted;
						cancelAllWanted = false;
						aborts.swap(cancelWanted);
						// FIFO, but a request whose host has both slots busy doesn't leave the others waiting
						for (auto it = waiting.begin(); it != waiting.end() && active.size() + starts.size() < kMaxActive;)
						{
							auto h = perHost.find((*it)->host);
							if (h == perHost.end() || h->second < kMaxPerHost)
							{
								starts.push_back(std::move(*it));
								it = waiting.erase(it);
							}
							else
							{
								++it;
							}
						}
					}
				}
				if (leaving) break;

				for (auto it = active.begin(); it != active.end();)
				{
					Request* r = it->get();
					if (all || aborts.count(r->id))
					{
						std::string host = r->host;
						if (r->easy) curl_multi_remove_handle(mh, r->easy);
						discard(*r);
						auto h = perHost.find(host);
						if (h != perHost.end() && --h->second <= 0) perHost.erase(h);
						it = active.erase(it);
					}
					else
					{
						++it;
					}
				}

				for (auto& up : starts)
				{
					std::unique_ptr<Request> r = std::move(up);
					if (!prepare(*r))
					{
						finish(std::move(r), CURLE_FAILED_INIT);
						continue;
					}
					CURLMcode mc = curl_multi_add_handle(mh, r->easy);
					if (mc != CURLM_OK)
					{
						r->error = curl_multi_strerror(mc);
						finish(std::move(r), CURLE_FAILED_INIT);
						continue;
					}
					perHost[r->host]++;
					active.push_back(std::move(r));
				}

				if (active.empty())
				{
					// nothing on the wire: sleep until a request comes, or a cancel wants the worker's idle attention
					// (a cancelAll must be consumed here even with nothing running, or the next request would suffer)
					std::unique_lock<std::mutex> g(m);
					if (!stopping)
						cv.wait(g, [this] { return stopping || !waiting.empty() || !cancelWanted.empty() || cancelAllWanted; });
					continue;
				}

				int running = 0;
				CURLMcode mc = curl_multi_perform(mh, &running);
				int left = 0;
				while (CURLMsg* msg = curl_multi_info_read(mh, &left))
				{
					if (msg->msg != CURLMSG_DONE) continue;
					auto it = active.begin();
					while (it != active.end() && (*it)->easy != msg->easy_handle) ++it;
					if (it == active.end())
					{
						curl_multi_remove_handle(mh, msg->easy_handle);
						continue;
					}
					std::unique_ptr<Request> r = std::move(*it);
					active.erase(it);
					auto h = perHost.find(r->host);
					if (h != perHost.end() && --h->second <= 0) perHost.erase(h);
					curl_multi_remove_handle(mh, msg->easy_handle);
					finish(std::move(r), msg->data.result);
				}
				if (mc == CURLM_CALL_MULTI_PERFORM) continue;	// (old curl: perform again at once)
				if (mc != CURLM_OK)
				{
					// the multi itself broke: fail what runs and let the next request make a fresh try
					for (auto& r : active)
					{
						if (r->easy) curl_multi_remove_handle(mh, r->easy);
						r->error = curl_multi_strerror(mc);
						finish(std::move(r), CURLE_FAILED_INIT);
					}
					active.clear();
					perHost.clear();
					continue;
				}
				if (active.empty()) continue;	// just finished the last one: start a waiting request at once
				// without curl_multi_wakeup a short wait is the only way a new request or a cancel gets seen
#if LIBCURL_VERSION_NUM >= 0x074400
				curl_multi_poll(mh, nullptr, 0, 250, nullptr);
#else
				curl_multi_poll(mh, nullptr, 0, 100, nullptr);
#endif
			}

			// a quit: transfers are dropped where they are, never waited for; curl's sockets close with the handles
			for (auto& r : active)
			{
				if (r->easy) curl_multi_remove_handle(mh, r->easy);
				discard(*r);
			}
			active.clear();
			{
				std::lock_guard<std::mutex> g(m);
				multiAlive = false;
				multi = nullptr;
				waiting.clear();
			}
			if (mh) curl_multi_cleanup(mh);
		}

		std::mutex m;
		std::condition_variable cv;
		std::deque<std::unique_ptr<Request>> waiting;	// FIFO, shared with get()/cancel()
		std::vector<Result> results;
		std::set<uint64_t> cancelWanted;
		std::string userAgent;
		CURLM* multi = nullptr;
		std::thread thread;
		Uint32 wakeEvent = 0;
		uint64_t nextId = 1;
		bool started = false, stopping = false, multiAlive = false, eventPending = false, cancelAllWanted = false;
	};

	MangaFetch::MangaFetch() : impl(new Impl)
	{
		// libcurl sets itself up once, before its first use on a thread; net_ftp does the same, and libcurl allows
		// repeated init calls (each one is only counted)
		static const bool once = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
		(void)once;
#ifdef BP_VERSION
		impl->userAgent = std::string("BananaPlayer/") + BP_VERSION;
#else
		impl->userAgent = "BananaPlayer";
#endif
	}

	// the owner never destroys it (the thread outlives the player's loop); this only keeps a misuse from leaving the
	// thread fetching a state that is going away
	MangaFetch::~MangaFetch()
	{
		stop();
	}

	void MangaFetch::start(Uint32 wakeEvent)
	{
		std::shared_ptr<Impl> d = impl;
		std::lock_guard<std::mutex> g(d->m);
		if (d->started || d->stopping) return;
		d->started = true;
		d->wakeEvent = wakeEvent;
		// detached, as the other workers: a quit must not join a thread that may be in a write callback
		d->thread = std::thread([d] { d->run(); });
		d->thread.detach();
	}

	uint64_t MangaFetch::get(const std::string& url, const Headers& headers, int64_t maxBytes, const std::string& toFile)
	{
		std::shared_ptr<Impl> d = impl;
		std::unique_ptr<Impl::Request> r(new Impl::Request);
		r->url = url;
		r->host = hostOf(url);
		r->headers = headers;
		r->maxBytes = maxBytes;
		r->toFile = toFile;
		if (!toFile.empty()) r->part = toFile + ".part";
		std::lock_guard<std::mutex> g(d->m);
		if (d->stopping) return 0;
		r->id = d->nextId++;
		uint64_t id = r->id;
		d->waiting.push_back(std::move(r));
		d->wakeLocked();
		return id;
	}

	void MangaFetch::cancel(uint64_t id)
	{
		std::shared_ptr<Impl> d = impl;
		std::lock_guard<std::mutex> g(d->m);
		for (auto it = d->waiting.begin(); it != d->waiting.end(); ++it)
		{
			if ((*it)->id == id)
			{
				d->waiting.erase(it);
				return;
			}
		}
		for (auto it = d->results.begin(); it != d->results.end(); ++it)
		{
			if (it->id == id)
			{
				d->results.erase(it);
				return;
			}
		}
		// running now, or a get() the worker just took from the queue: the worker drops or aborts it
		d->cancelWanted.insert(id);
		d->wakeLocked();
	}

	void MangaFetch::cancelAll()
	{
		std::shared_ptr<Impl> d = impl;
		std::lock_guard<std::mutex> g(d->m);
		d->waiting.clear();
		d->results.clear();
		d->cancelWanted.clear();
		d->cancelAllWanted = true;
		d->wakeLocked();
	}

	std::vector<MangaFetch::Result> MangaFetch::take()
	{
		std::shared_ptr<Impl> d = impl;
		std::lock_guard<std::mutex> g(d->m);
		d->eventPending = false;
		std::vector<Result> out;
		out.swap(d->results);
		return out;
	}

	void MangaFetch::stop()
	{
		std::shared_ptr<Impl> d = impl;
		std::lock_guard<std::mutex> g(d->m);
		d->stopping = true;
		d->waiting.clear();
		d->cancelWanted.clear();
		d->cancelAllWanted = false;
		d->wakeLocked();
	}
}
