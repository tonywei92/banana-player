#pragma once
// The Manga app's real covers (docs/design/manga.md, section 7's "Covers on the bottom panel"): a cover is a page on
// the card (a picture file, or an entry inside a CBZ) decoded to the tile's own pixels. Decoding one is tens of
// milliseconds of software work, so a grid of them must never happen on the main thread.
//
// It also decodes a song's album art for Home's cards (wantSong): the same worker, since both are one picture to
// decode off the main thread, and one queue keeps a manga grid from being starved by two threads.
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
#include "artbackdrop.h"

namespace bp
{
	// The songs' album art carries this prefix: main sends those answers to Ui's song art cache (Home's cards,
	// the rail's Now playing key, Quick settings' block share it), every other key to Manga (the prefix keeps
	// a Manga series' id from ever colliding with one of these)
	inline constexpr const char* HOME_COVER_KEY = "home:";
	// The backdrops carry this one: their answers go to Ui's Match-album-art cache (the player's ground and the
	// top screen's, docs/redesign "Match album art") — kept apart from both the song art's and Manga's keys
	inline constexpr const char* BACKDROP_COVER_KEY = "bg:";

	class CoverWorker
	{
	public:
		struct Result
		{
			std::string key;			// the Ui's id for it (a series id)
			int w = 0, h = 0;			// the RGBA's own size: the tile's width, cropped to the tile's aspect
			std::vector<uint8_t> rgba;	// w * h * 4, rows top to bottom, straight (not premultiplied) alpha
			bool ok = false;			// false: nothing to draw (the generated cover stays)
			// a backdrop answer (backdrop set, the key "bg:"): the art's hash and its maths — one hue (or none),
			// the four percentile luminances and the 160 x 120 picture. ok false: the song has no art, so the
			// plain ground is the answer; a picture that is all fallback has ok true and no hue
			bool backdrop = false;
			uint64_t artHash = 0;
			artbg::Art art;
		};
		explicit CoverWorker(std::function<void()> ready) : ready(std::move(ready)) {}
		// key: the owner's id; source: LocalSeries::cover (a path, "archive.cbz#entry", or "archive.cbz#" for its first
		// page); w, h: the tile in the panel's own pixels. A cover already waiting keeps one place in the queue, moved
		// to the front
		void want(const std::string& key, const std::string& source, int w, int h);
		// a song's album art: the picture in the file, else cover/folder/front.* beside it (probeCover), decoded to the
		// card's own pixels. key carries Home's "home:" prefix, which is how main routes the answer back to it; the
		// crop is the middle of the picture, not a page's top (album art is not a manga page)
		void wantSong(const std::string& key, const std::string& path, int w, int h);
		// a song's backdrop (Match album art): the art's encoded bytes hashed, so the cache <state dir>/artbg/
		// answers without a decode; a miss decodes once and writes it. The key carries the "bg:" prefix, which
		// is how main routes the answer to Ui's backdrop. No art in the file or beside it: ok false
		void wantBackdrop(const std::string& key, const std::string& path);
		// the cache's folder, as main hands it in at start (empty: nothing is cached, the maths every time)
		void setStateDir(std::string dir) { stateDir = std::move(dir); }
		// the Manga app was read again: nothing of its queue is wanted any more (a decode under way finishes). Home's
		// cards are not the Manga app's to cancel: their "home:" requests stay
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
			bool song = false;	// source is a song file: its embedded art, else the cover beside it
			bool backdrop = false;	// source is a song file: its backdrop (the cache, the maths), not a tile
		};
		void ask(const std::string& key, const std::string& source, int w, int h, bool song, bool backdrop = false);
		void run();
		std::function<void()> ready;
		std::string stateDir;	// the artbg/ cache's folder (the worker thread alone walks it)
		std::thread thread;
		std::mutex m;
		std::condition_variable cv;
		std::vector<Item> items;	// newest at the back: popped there
		std::vector<Result> results;
		bool signalled = false, stopping = false, started = false;
	};
}
