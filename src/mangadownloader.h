#pragma once
// The manga downloads' engine (docs/design/manga.md section 7): one chapter at a time, two pages at once, pages into a
// work folder and a stored CBZ in the downloads folder. It has no thread of its own: main's loop calls pump() every
// pass and hands every fetch answer to onFetched() before the Ui sees it, so the fetcher's thread does the waiting and
// a slow server never stalls a panel. A restart resumes at the first page missing from the work folder.
#include "dlqueue.h"
#include "fetchapi.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bp
{
	class MangaDownloader
	{
	public:
		// what the Downloads page shows of the running chapter; runningId is empty when idle
		struct Status
		{
			std::string runningId;
			int done = 0, total = 0;
			uint64_t bytes = 0;
			bool lowSpace = false;
		};

		// Under this the queue stops starting chapters: a chapter is tens of MB and a card with less left has no room
		// for the next one (section 7). The recheck is slow because a freed card changes in minutes, not frames.
		static constexpr uint64_t kMinFreeBytes = 500ull << 20;
		static constexpr int64_t kSpaceRecheckMs = 30 * 1000;
		static constexpr int kMaxAttempts = 3;	// one try and two retries, then the chapter fails

		explicit MangaDownloader(FetchApi& fetch, DownloadQueue& queue) : fetch_(&fetch), queue_(queue) {}
#ifdef BP_MANGA_FETCH
		// (the player only, which defines BP_MANGA_FETCH: the test has no fetcher to link, and GCC keeps the adapter's
		// virtual functions, which call it, even unused)
		MangaDownloader(MangaFetch& fetch, DownloadQueue& queue)
			: owned_(new MangaFetchApi(fetch)), fetch_(owned_.get()), queue_(queue) {}
#endif
		MangaDownloader(const MangaDownloader&) = delete;
		MangaDownloader& operator=(const MangaDownloader&) = delete;

		// downloadsRoot: <Files>/Manga; workDir: <state>/manga/downloading, one hashed folder per item id
		void setFolders(const std::string& downloadsRoot, const std::string& workDir);
		// bytes free on the downloads card; the default is statvfs(downloadsRoot)
		void setFreeSpaceProbe(std::function<uint64_t()> probe);

		// Called every pass: starts the next chapter when idle and the card has room, keeps two page requests in
		// flight for the running one. It takes nothing itself: answers reach onFetched() through main.
		void pump(int64_t nowMs);
		// Every fetch result goes here first: true when it was one of this engine's page requests
		bool onFetched(const MangaFetch::Result& r);

		// Stop the requests in flight; a paused chapter continues at its next page when the queue resumes, and the
		// cancelled one is left for the Downloads page (the queue keeps it paused, not running).
		void pauseAll();
		void cancel(const std::string& id);

		Status status() const { return st_; }
		uint64_t serial() const { return serial_; }	// grows on every change: the Ui redraws on it

		// FAT-safe: no / \ : * ? " < > | and no control characters, trimmed of spaces and dots, at most 120
		// characters (never cutting a UTF-8 sequence). A chapter or series name is a server's text, not a path.
		static std::string safeName(const std::string& name);

	private:
		struct Flight
		{
			uint64_t fetchId = 0;
			int page = 0;	// 0-based, reading order
		};
		struct Retry
		{
			int page = 0;
			int64_t at = 0;
		};

		std::string itemDir(const std::string& id) const;
		std::string pagePath(int page, const std::string& ext) const;
		void scanWork();	// pages an earlier run left, by their NNNN.ext names
		void startItem(const DlItem& it);
		void resetRun();
		bool issue(int page);
		void advance();	// count the pages present, in order, so a gap is never counted past
		void finishItem();
		void failItem(const std::string& error);
		bool writeChapter(std::string& error);
		bool spaceOk(int64_t nowMs);
		void dropFlights(bool abandoned);	// cancel them; a pause isn't a failed attempt
		void touch() { serial_++; }

		std::unique_ptr<FetchApi> owned_;
		FetchApi* fetch_ = nullptr;
		DownloadQueue& queue_;
		std::string downloadsRoot_, workDir_;
		std::function<uint64_t()> probe_;

		bool running_ = false;
		bool lowSpace_ = false;	// what the last probe said; the queue's flag is the same fact for next()
		DlItem item_;	// a copy: the queue's pointer dies at its next change
		std::string dir_;	// its work folder
		std::vector<std::string> present_;	// per page, the file there or ""
		std::vector<int> attempts_;	// requests sent for each page
		std::vector<Flight> flights_;
		std::vector<Retry> retries_;
		int done_ = 0;	// pages present in order, 0-based count
		int reported_ = 0;	// what queue.pageDone has been told so far (a resumed item may be ahead)
		int total_ = 0;
		int64_t lastNow_ = 0;	// onFetched has no clock: a retry waits from the last pump
		int64_t lastSpaceCheck_ = -1;
		Status st_;
		uint64_t serial_ = 0;
	};
}
