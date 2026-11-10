#pragma once
// The Manga app's real covers (docs/design/manga.md, section 7's "Covers on the bottom panel"): a cover is a page on
// the card (a picture file, or an entry inside a CBZ) decoded to the tile's own pixels. Decoding one is tens of
// milliseconds of software work, so a grid of them must never happen on the main thread.
//
// One thread, one cover at a time, the newest request first (the grid scrolls away from what was asked before it). At
// most 200 wait: a fast scroll through a big library can't build a queue nobody will see. Like the Updater and
// DurationWorker it is made on the heap and never destroyed (its thread may be inside libavcodec at exit). It never
// touches Ui or Player: answers go to a mutex-guarded list the main thread takes, and ready() (on the worker's thread)
// tells the loop there are some.
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bp
{
	class CoverWorker
	{
	public:
		struct Result
		{
			std::string key;			// the Ui's id for it (a series id)
			int w = 0, h = 0;			// the RGBA's own size: the tile's width, cropped to the tile's aspect
			std::vector<uint8_t> rgba;	// w * h * 4, rows top to bottom, straight (not premultiplied) alpha
			bool ok = false;			// false: nothing to draw (the generated cover stays)
		};
		explicit CoverWorker(std::function<void()> ready) : ready(std::move(ready)) {}
		// key: the owner's id; source: LocalSeries::cover (a path, or "archive.cbz#entry"); w, h: the tile in the
		// panel's own pixels. A cover already waiting keeps one place in the queue, moved to the front
		void want(const std::string& key, const std::string& source, int w, int h);
		// the app closed or was read again: nothing of the queue is wanted any more (a decode under way finishes)
		void cancel();
		// main thread: the answers since the last take
		std::vector<Result> take();
		// at quit: no more work (the thread waits for ever)
		void stop();

	private:
		struct Item
		{
			std::string key, source;
			int w = 0, h = 0;
		};
		void run();
		std::function<void()> ready;
		std::thread thread;
		std::mutex m;
		std::condition_variable cv;
		std::vector<Item> items;	// newest at the back: popped there
		std::vector<Result> results;
		bool signalled = false, stopping = false, started = false;
	};
}
