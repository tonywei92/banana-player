// The media index's thread (mediaindex.h is the index itself). A pass at start, one whenever main says the cards
// changed, and nothing else; between every file it waits out whatever has the card (setBusy: a video's picture, the
// Manga engine, a file opening) and rests a moment, so the walk never stutters playback.
//
// Made on the heap and never destroyed (like DurationWorker and CoverWorker: its thread may be inside libavformat
// at exit). It never touches Ui or Player: the answer is committed to the MediaIndex the UI will read (a shared_ptr
// and a generation number), and ready() — called on this thread — posts the SDL user event that says a pass is done.
#pragma once
#include "mediaindex.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace bp
{
	class MediaIndexWorker
	{
	public:
		MediaIndexWorker(MediaIndex& shared, std::function<void()> ready) : shared(shared), ready(std::move(ready)) {}
		// the state folder (its media-index.tsv keeps the tags across runs); "": tags are read, never kept
		void configure(const std::string& stateDir);
		// the first pass (the thread starts here, as the other workers': not before there is work)
		void start();
		// the cards were touched (a card put in or taken out, a Refresh): walk again
		void refresh();
		// true while the card is someone else's: the walk waits it out, between files
		void setBusy(bool busy);
		void stop();

		// what the UI reads (Music, Videos, Search): snapshot(), generation()
		MediaIndex& index() { return shared; }

	private:
		void run();
		void pass();
		MediaIndex& shared;
		std::function<void()> ready;
		std::string stateDir;
		std::thread thread;
		std::mutex m;
		std::condition_variable cv;
		bool started = false, again = false, stopping = false;
		std::atomic<bool> busy{false};
	};
}
