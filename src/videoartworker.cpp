#include "videoartworker.h"
#include "filesave.h"
#include "treecopy.h"
#include "videoframe.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/time.h>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace bp
{
	namespace
	{
		// the folder's cap: ~85 KB a video (a 960-wide frame, a thumbnail, the info), so about a thousand videos
		const uint64_t CACHE_BYTES = 96ull << 20;
		const int LARGE_W = 960, SMALL_W = 128, SMALL_H = 72;

		bool readText(const std::string& path, std::string& out)
		{
			std::ifstream f(path, std::ios::binary);
			if (!f) return false;
			std::ostringstream s;
			s << f.rdbuf();
			out = s.str();
			return true;
		}

		// a file's bytes, -1 when it isn't there
		long long fileBytes(const std::string& path)
		{
			struct stat st;
			return stat(path.c_str(), &st) == 0 ? (long long)st.st_size : -1;
		}

		// a hit: its mtime now, so the trim (the oldest first) keeps what is looked at
		void touch(const std::string& path) { utimes(path.c_str(), nullptr); }

		PageImage readJpeg(const std::string& path, int w)
		{
			PageImage img = decodePageFile(path, w);
			return img.ok() ? img : PageImage{};
		}
	}

	void VideoArtWorker::setDir(std::string d)
	{
		mkdir(d.substr(0, d.rfind('/')).c_str(), 0755);	// (<state dir>/cache)
		mkdir(d.c_str(), 0755);
		std::lock_guard<std::mutex> g(m);
		dir = std::move(d);
	}

	void VideoArtWorker::start()
	{
		if (started || stopping) return;
		started = true;
		thread = std::thread([this] { run(); });
		thread.detach();
	}

	void VideoArtWorker::want(const std::string& path, bool large)
	{
		std::lock_guard<std::mutex> g(m);
		if (stopping || dir.empty()) return;
		for (size_t i = 0; i < items.size(); i++)
			if (!items[i].record && items[i].path == path)
			{
				large = large || items[i].large;
				items.erase(items.begin() + (long)i);
				break;
			}
		items.push_back({path, large, false, {}});
		if (items.size() > 200) items.erase(items.begin());
		start();
		cv.notify_one();
	}

	void VideoArtWorker::record(const std::string& path, const VideoMeta& meta)
	{
		std::lock_guard<std::mutex> g(m);
		if (stopping || dir.empty()) return;
		items.push_back({path, false, true, meta});
		start();
		cv.notify_one();
	}

	void VideoArtWorker::setBusy(bool b)
	{
		if (busy.exchange(b) == b || b) return;
		// free again: the misses held back go first, as they were asked
		std::lock_guard<std::mutex> g(m);
		items.insert(items.end(), waiting.begin(), waiting.end());
		waiting.clear();
		cv.notify_one();
	}

	std::vector<VideoArtWorker::Result> VideoArtWorker::take()
	{
		std::lock_guard<std::mutex> g(m);
		signalled = false;
		std::vector<Result> out;
		out.swap(results);
		return out;
	}

	void VideoArtWorker::stop()
	{
		std::lock_guard<std::mutex> g(m);
		stopping = true;
		items.clear();
		waiting.clear();
		cv.notify_one();
	}

	VideoArtWorker::Result VideoArtWorker::answer(const Item& it, bool& deferred, double& makeMs)
	{
		Result r;
		r.path = it.path;
		r.largeAsked = it.large;
		deferred = false;
		makeMs = 0;
		struct stat st;
		if (stat(it.path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
		{
			r.noPicture = true;
			return r;
		}
		VideoCacheFiles f = videoCacheFiles(dir, videoCacheName(it.path, (long long)st.st_size, (int64_t)st.st_mtime));
		if (it.record)
		{
			r.meta = it.meta;
			r.metaOk = true;	// (shown even if the card refused the write)
			writeFileAtomic(f.meta, videoMetaText(it.meta));
			writes++;
			return r;
		}
		std::string text;
		bool haveMeta = readText(f.meta, text) && parseVideoMeta(text, r.meta);
		long long smallBytes = fileBytes(f.small);
		// (a frame asked for whose file the trim took: made again with the rest)
		bool frameThere = !it.large || smallBytes == 0 || fileBytes(f.large) > 0;
		if (haveMeta && smallBytes >= 0 && frameThere)
		{
			// the cache's answer: a 0-byte thumbnail is "no picture", kept so the video isn't opened again
			r.metaOk = true;
			touch(f.meta);
			touch(f.small);
			if (smallBytes == 0) r.noPicture = true;
			else
			{
				r.small = readJpeg(f.small, SMALL_W);
				if (it.large)
				{
					touch(f.large);
					r.large = readJpeg(f.large, LARGE_W);
				}
			}
			return r;
		}
		if (busy.load())
		{
			deferred = true;
			return r;
		}
		// a miss: the video opened once, all three written
		auto t0 = std::chrono::steady_clock::now();
		VideoGrab g;
		bool opened = grabVideo(it.path, g, LARGE_W, SMALL_W, SMALL_H);
		makeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		// (info mpv wrote while the video played says more than the header: it stays)
		bool keepMeta = haveMeta && r.meta.fromPlayer;
		if (!keepMeta) r.meta = g.meta;
		r.metaOk = keepMeta || (opened && g.metaOk);
		if (!opened && makeMs >= 5000)
		{
			// cut by the deadline (a slow card, a busy disk): not the file's fault, so nothing is written (the next
			// run asks again); this run shows the kind's mark
			fprintf(stderr, "video art: %s timed out (%.0f ms)\n", it.path.c_str(), makeMs);
			r.noPicture = true;
			return r;
		}
		std::string small = encodeJpeg(g.small), large = encodeJpeg(g.large);
		if (!keepMeta) writeFileAtomic(f.meta, videoMetaText(g.meta));
		writeFileAtomic(f.small, small);	// (empty: no picture, for good)
		if (!large.empty()) writeFileAtomic(f.large, large);
		writes += 3;
		r.noPicture = small.empty();
		fprintf(stderr, "video art: made %s in %.0f ms (%dx%d frame, %zu + %zu bytes%s)\n", it.path.c_str(), makeMs,
			g.large.w, g.large.h, small.size(), large.size(), r.noPicture ? ", no picture" : "");
		r.small = std::move(g.small);
		if (it.large) r.large = std::move(g.large);
		return r;
	}

	void VideoArtWorker::run()
	{
#ifdef __linux__
		// this thread only: playback and the panels first; its reads too (the best-effort class's lowest level)
		pid_t tid = (pid_t)syscall(SYS_gettid);
		setpriority(PRIO_PROCESS, tid, 15);
		syscall(SYS_ioprio_set, 1 /* IOPRIO_WHO_PROCESS */, tid, (2 << 13) | 7 /* BE, level 7 */);
#endif
		{
			std::string d;
			{
				std::lock_guard<std::mutex> g(m);
				d = dir;
			}
			uint64_t left = trimCacheFolder(d, CACHE_BYTES);
			fprintf(stderr, "video art: cache %s, %.1f MB\n", d.c_str(), left / 1048576.0);
		}
		for (;;)
		{
			Item it;
			{
				std::unique_lock<std::mutex> g(m);
				cv.wait(g, [&] { return !stopping && !items.empty(); });
				it = std::move(items.back());
				items.pop_back();
			}
			bool deferred;
			double ms;
			Result r = answer(it, deferred, ms);
			bool signal = false;
			{
				std::lock_guard<std::mutex> g(m);
				if (deferred) waiting.push_back(std::move(it));
				else if (!stopping)
				{
					results.push_back(std::move(r));
					signal = !signalled;
					signalled = true;
				}
				// held back while busy, then freed between the check and here: not left waiting for the next change
				if (deferred && !busy.load())
				{
					items.insert(items.end(), waiting.begin(), waiting.end());
					waiting.clear();
				}
			}
			if (signal) ready();
			if (writes >= 60)
			{
				writes = 0;
				trimCacheFolder(dir, CACHE_BYTES);
			}
			// a pause as long as the making (a core's half at most), so a folder of new videos never hogs one
			if (ms > 0) std::this_thread::sleep_for(std::chrono::microseconds((long long)(ms * 1000)));
		}
	}
}
