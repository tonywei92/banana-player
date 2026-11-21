// The videos' thumbnails, frames and info (videocache.h), made on a thread of their own and kept on the card. A want
// whose files are in the cache is read back (two small JPEGs and a few lines: milliseconds); a miss opens the video
// once (videoframe.h: its header and one decoded frame) and writes all three. Low priority: a lowered nice and I/O
// class on Linux, a pause after each frame made, and no frame made at all while setBusy holds (a file opening, a video
// playing: the decode would take a core from mpv) — those wants wait, cached answers still go out.
//
// Made on the heap and never destroyed (like DurationWorker): its thread may be inside libavcodec at exit. It never
// touches Ui or Player: answers go to a mutex-guarded list the main thread takes; ready() (on the worker's thread)
// says there are some, once until take().
#pragma once
#include "pagedecode.h"
#include "videocache.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bp
{
	class VideoArtWorker
	{
	public:
		struct Result
		{
			std::string path;
			bool metaOk = false;	// meta holds the file's facts
			VideoMeta meta;
			PageImage small;	// the list's thumbnail (128 x 72); empty: none (noPicture says if for good)
			PageImage large;	// the top screen's frame, when it was asked for (at most 960 wide)
			bool largeAsked = false;
			bool noPicture = false;	// the video gives none (audio only, a codec this build lacks): never asked again
		};
		explicit VideoArtWorker(std::function<void()> ready) : ready(std::move(ready)) {}
		// the cache's folder (<state dir>/cache/videos), made when it isn't there; before the first want
		void setDir(std::string dir);
		// a video's thumbnail and info, and its large frame when `large`; asked again it moves to the front (the
		// newest first: the list scrolled to it). At most 200 wait
		void want(const std::string& path, bool large);
		// mpv's facts for a video it opened (the exact length, every track, a subtitle file beside it): the cached
		// info is written again from them and answered (a want waiting behind busy doesn't hold it up)
		void record(const std::string& path, const VideoMeta& meta);
		void setBusy(bool busy);
		std::vector<Result> take();	// main thread: the answers since the last take
		void stop();	// at quit: nothing more (the thread waits for ever)

	private:
		struct Item
		{
			std::string path;
			bool large = false;
			bool record = false;	// meta holds mpv's facts to write
			VideoMeta meta;
		};
		void run();
		void start();	// under lock: the thread, the first time there's work
		Result answer(const Item& it, bool& deferred, double& makeMs);
		std::function<void()> ready;
		std::string dir;
		std::thread thread;
		std::mutex m;
		std::condition_variable cv;
		std::vector<Item> items;	// newest at the back: popped there
		std::vector<Item> waiting;	// misses held back while busy
		std::vector<Result> results;
		int writes = 0;	// since the last trim
		bool signalled = false, stopping = false, started = false;
		std::atomic<bool> busy{false};
	};
}
