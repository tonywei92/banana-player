// The media index's thread (see mediaworker.h). The pass: the tag cache loaded (or not, with no state folder), the
// cards' Music and Videos folders walked with the libavformat reader, the snapshot grouped and committed, one
// summary line in the log.
#include "mediaworker.h"
#include "mediatags.h"
#include "durations.h"
#include <chrono>
#include <cstdio>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace bp
{
	void MediaIndexWorker::configure(const std::string& dir)
	{
		std::lock_guard<std::mutex> g(m);
		stateDir = dir;
	}

	void MediaIndexWorker::start()
	{
		std::lock_guard<std::mutex> g(m);
		if (started || stopping) return;
		started = true;
		again = true;
		thread = std::thread([this] { run(); });
		thread.detach();
		cv.notify_one();
	}

	void MediaIndexWorker::refresh()
	{
		std::lock_guard<std::mutex> g(m);
		if (!started || stopping) return;
		again = true;
		cv.notify_one();
	}

	void MediaIndexWorker::setBusy(bool b)
	{
		if (busy.exchange(b) && !b) cv.notify_one();
	}

	void MediaIndexWorker::stop()
	{
		std::lock_guard<std::mutex> g(m);
		stopping = true;
		cv.notify_one();
	}

	void MediaIndexWorker::run()
	{
#ifdef __linux__
		// this thread only, as the file rows' prober: playback and the panels go first, and its reads too
		pid_t tid = (pid_t)syscall(SYS_gettid);
		setpriority(PRIO_PROCESS, tid, 15);
		syscall(SYS_ioprio_set, 1 /* IOPRIO_WHO_PROCESS */, tid, (2 << 13) | 7 /* BE, level 7 */);
#endif
		for (;;)
		{
			std::unique_lock<std::mutex> g(m);
			cv.wait(g, [&] { return stopping || again; });
			if (stopping) return;
			again = false;
			g.unlock();
			pass();
			// a pass that was stopped left `again` alone: a refresh that came in meanwhile still runs
			g.lock();
		}
	}

	void MediaIndexWorker::pass()
	{
		auto t0 = std::chrono::steady_clock::now();
		std::string dir;
		{
			std::lock_guard<std::mutex> g(m);
			dir = stateDir;
			// this pass's counters, from nothing: the pages' reading notice reads them as the walk goes
			reading = true;
			live.card.store(-1);
			live.foldersDone.store(0);
			live.foldersKnown.store(0);
			live.songs.store(0);
			live.videos.store(0);
		}
		// the cache is this pass's alone: loaded, used, dropped with the walk's other working memory
		MediaTagCache cache;
		if (!dir.empty() && !cache.load(dir + "/media-index2.tsv"))
			fprintf(stderr, "media index: %s/media-index2.tsv couldn't be written\n", dir.c_str());
		MediaFolders folders = mediaFolders();
		MediaWalk walk;
		// the walk's two helpers, both this thread's:
		// the tags through libavformat, and a known length into the shared duration cache (the file rows and the
		// dashboard never open these files again to learn one)
		TagReader reader = [](const std::string& path, long long size, int64_t mtime, MediaTags& out) {
			bool ok = probeMediaTags(path, size, out);
			if (out.duration > 0) DurationCache::shared().put(durationKey(path, size, mtime), out.duration);
			return ok;
		};
		int calls = 0;
		WalkGate gate = [&] {
			if (stopping) return false;
			// small batches: the card is never all the walk's, even with nothing playing
			if (++calls % 16 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
			// a video, the Manga engine, a file opening: theirs, until it's done with
			while (!stopping && busy.load()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
			return !stopping;
		};
		bool whole = walkMedia(folders, reader, cache, gate, walk, &live);
		{
			std::lock_guard<std::mutex> g(m);
			reading = false;		// (the counters stay for the notice's last drawing until the answer lands)
		}
		if (!whole) return;	// (stopped mid-way: not a whole answer)
		for (auto& f : walk.failed) fprintf(stderr, "media index: couldn't read %s\n", f.c_str());
		int musicVideos = 0;
		for (auto& s : walk.songs) musicVideos += s.video ? 1 : 0;
		std::shared_ptr<const MediaSnapshot> snap = groupMediaSnapshot(std::move(walk.words), std::move(walk.songs),
			std::move(walk.videos), std::move(walk.musicFolders), std::move(walk.videoFolders));
		shared.commit(snap);
		double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		fprintf(stderr, "media index: %d Music, %d Videos folders; %d songs (%d a video), %d albums, %d artists; "
			"%d videos; tags %d read, %d kept; %.0f ms\n",
			(int)snap->musicFolders.size(), (int)snap->videoFolders.size(),
			(int)snap->songs.size(), musicVideos, (int)snap->albums.size(), (int)snap->artists.size(),
			(int)snap->videos.size(), walk.probed, walk.kept, ms);
		ready();
	}
}
