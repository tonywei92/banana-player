#pragma once
// The manga downloads' queue: what waits for the card and where each chapter stopped (docs/design/manga.md, section 7).
// One chapter at a time and two pages at once are the worker's rules; this is the order, the states and the TSV that
// lets a quit resume at the next page not written. No window, no threads, no network: the image thread drives it.
#include <cstdint>
#include <string>
#include <vector>

namespace bp
{
	// One chapter waiting for the card. id is the key ("seriesId\nchapterId"), so a chapter queued twice is one item.
	struct DlItem
	{
		std::string id;
		std::string seriesId, seriesTitle, chapterId, chapterTitle, source;
		std::vector<std::string> pageUrls;	// reading order; what is written is a prefix of it
		int done = 0;	// pages written so far, in order: a retry starts again at pageUrls[done]
		enum State { Waiting, Running, Paused, Failed, Done };
		State state = Waiting;
		std::string error;	// why it failed, for the Downloads page
		int64_t added = 0;	// seconds since the epoch, as the rest of the player counts
		uint64_t bytes = 0;	// what the pages written so far took
		std::string target;	// the final .cbz path
	};

	class DownloadQueue
	{
	public:
		// A damaged or hand-edited file must never keep the reader from starting, so a line that doesn't parse is
		// skipped; the caps stop one file from growing without end.
		static constexpr size_t MAX_ITEMS = 2000;
		static constexpr size_t MAX_PAGES = 5000;

		// <stateDir>/manga/downloads-queue.tsv. False only when there is no state folder. A Running item comes back
		// Waiting: a quit mid-chapter resumes at its next page.
		bool load(const std::string& stateDir);
		// Written beside and renamed over, as the other stores write. Done items are not written (the chapter is on
		// the card now); they stay in memory until clearDone().
		bool save() const;

		void add(DlItem item);	// ignored when its id is empty or already queued, or past a cap
		void remove(const std::string& id);
		void pause(const std::string& id);	// Waiting or Running
		// Paused -> Waiting; one chapter asked for is also downloads running again, so this lifts an all-pause
		// (leaving it on would make the button do nothing).
		void resume(const std::string& id);
		void pauseAll();
		void resumeAll();	// an item paused on its own stays paused
		bool allPaused() const { return allPaused_; }
		// The next chapter to fetch: the first Waiting one in the order they were added, or null while the queue is
		// all-paused or the card is low. The worker asks only when it isn't already on a chapter (one at a time).
		// The pointer is good until the next change.
		const DlItem* next() const;
		// The worker's report: under 500 MB free on the card. Nothing new starts; the running chapter keeps going,
		// since its pages are nearly here.
		void setLowSpace(bool low);
		// The worker took it. A Paused one stays paused: a pause pressed after next() wins.
		void started(const std::string& id);
		void pageDone(const std::string& id, uint64_t bytes);	// done grows; it stops at the list's end
		void failed(const std::string& id, const std::string& error);	// done is kept, so a retry resumes there
		void retry(const std::string& id);	// Failed -> Waiting
		void finished(const std::string& id);	// Done; stays in items() until clearDone()
		void clearDone();
		std::vector<DlItem> items() const;	// queue order, Done included
		int pendingPages() const;	// pages not written yet, Done items aside
		uint64_t serial() const { return serial_; }	// grows with every change: the UI redraws on it

	private:
		DlItem* find(const std::string& id);
		const DlItem* find(const std::string& id) const;
		void parse(const std::string& text);
		std::string serialize() const;

		std::string dir_;	// the state folder; the file is in <dir>/manga
		std::vector<DlItem> items_;
		// A session hold only: a quit mid-chapter resumes on the next start (section 7).
		bool allPaused_ = false;
		bool lowSpace_ = false;
		uint64_t serial_ = 0;
	};
}
