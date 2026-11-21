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
		// the state folder (its media-index2.tsv keeps the tags across runs); "": tags are read, never kept
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

		// the pass's live counters, for the reading notice's bar ("music-reading", "videos-reading"): a copy on
		// the main thread whenever a page asks. reading: the pass is on (a second pass over an unchanged card
		// spends its time in the opendir walk, so the bar moves there too)
		struct Progress
		{
			bool reading = false;
			int card = -1;		// mediaCards()'s index of the one being walked (-1 none)
			int foldersDone = 0, foldersKnown = 0;
			int songs = 0, videos = 0;	// read so far this pass
		};
		Progress progress()
		{
			std::lock_guard<std::mutex> g(m);
			Progress p;
			p.reading = reading;
			p.card = live.card.load();
			p.foldersDone = live.foldersDone.load();
			p.foldersKnown = live.foldersKnown.load();
			p.songs = live.songs.load();
			p.videos = live.videos.load();
			return p;
		}

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
		bool reading = false;		// a pass is on (m's own, read by progress())
		WalkCounters live;		// the walk's counters, this pass's
	};
}
