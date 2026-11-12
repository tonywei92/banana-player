#pragma once
// The manga reader's library and reading progress: what the reader must remember across restarts
// (docs/design/manga.md, section 6). Two TSV files in <state>/manga: library.tsv and progress.tsv. Plain data, no
// window, no threads and no clock of its own: the reader's pages read it and the shell saves it.
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace bp
{
	// A series the user kept. The id is the caller's own and is the key everywhere: "local:/data/Manga/Ember Lantern"
	// (a source and a key in one) for a series, the chapter's own path for a chapter.
	struct MangaSeries
	{
		std::string id, title, source, cover;
		int64_t added = 0, lastRead = 0;	// seconds since the epoch, as the rest of the player counts
		bool inLibrary = true;				// false: taken out of the library but kept (a source not installed)
		std::string readingMode;			// "" is the setting's; else "rtl", "ltr" or "strip" (this series' own)
	};

	// Where a chapter was left. page counts from 0.
	struct ChapterProgress
	{
		std::string seriesId, chapterId, chapterTitle;
		int page = 0, pages = 0;	// pages: the chapter's length; 0 when it isn't known
		bool read = false;
		bool hidden = false;		// cleared from the History; where to resume stays
		int64_t at = 0;	// when it was last here
	};

	class MangaStore
	{
	public:
		// Neither file may grow without end: the oldest progress falls off first, the oldest series after that
		static constexpr size_t MAX_SERIES = 5000;
		static constexpr size_t MAX_PROGRESS = 20000;

		// The state folder; the files are in <dir>/manga. A file that isn't there is an empty store and a line that
		// doesn't parse is skipped, so a damaged file never keeps the reader from starting. False only when there is
		// nowhere to read from (dir "").
		bool load(const std::string& dir);
		// Both files, each written beside and renamed over, so a crash never leaves half a file. False when they
		// can't be written.
		bool save() const;

		// The library, as the Library page lists it: the ones read most recently first, then by title.
		std::vector<MangaSeries> library() const;
		// The series with this id, or null; the pointer is good until the next change.
		const MangaSeries* series(const std::string& id) const;
		void putSeries(const MangaSeries& s);	// added or changed; past MAX_SERIES the oldest added falls off
		void setInLibrary(const std::string& id, bool on);
		void removeSeries(const std::string& id);	// and its chapters' progress
		// A series found again under a new id (an extension's, after the manga engine was installed afresh: its ids
		// start over): its entry, its heart and its chapters' places move from `from` to `to`. A `to` already saved
		// (opened from Browse meanwhile) keeps its own fields, with the earlier added date and either heart
		void moveSeries(const std::string& from, const std::string& to);
		// chapters' places moved to new chapter ids (old id -> new id); an id not in the map stays as it is
		void moveChapters(const std::unordered_map<std::string, std::string>& ids);
		// every saved place of one series' chapters
		std::vector<ChapterProgress> progressOf(const std::string& seriesId) const;

		// The chapter's place, added or changed. A chapter read to its last page counts as read, and the series'
		// lastRead moves to p.at.
		// history false (Incognito mode): the place and the read mark are kept as ever, but the History doesn't move:
		// a chapter new to the store starts hidden from it, one already there keeps its time and its row, and the
		// series' last read stays as it was
		void setProgress(const ChapterProgress& p, bool history = true);
		const ChapterProgress* progress(const std::string& chapterId) const;	// null when there is none
		// What the Continue reading strip shows: each series in the library's latest unfinished chapter, newest first,
		// at most max. A chapter that was never opened (a record of it only says it is unread) isn't in it.
		std::vector<ChapterProgress> continueReading(int max) const;
		// Every chapter opened, newest first, at most max: the History page. A chapter cleared from the History
		// isn't in it.
		std::vector<ChapterProgress> history(int max) const;
		// The History page read as series: each series' newest chapter that isn't cleared from the History, the
		// series themselves newest first, at most max. (The page lists one row per series, not per chapter.)
		std::vector<ChapterProgress> historyBySeries(int max) const;
		// Read or unread for the chapters given; a chapter never opened becomes a record of, so it counts as unread.
		void markRead(const std::string& seriesId, const std::vector<std::string>& chapterIds, bool read);
		// The reading history: every chapter is hidden from the History, the reading progress stays (the read
		// marks and the page to resume).
		void clearHistory();
		// the History rows read at or after this time (seconds since the epoch) leave the list; the places stay
		void clearHistorySince(int64_t since);
		// one series' rows leave the list (History's Selected item); its places stay
		void clearHistorySeries(const std::string& seriesId);
		// How many of the chapters given aren't read.
		int unread(const std::string& seriesId, const std::vector<std::string>& chapterIds) const;

		uint64_t serial() const { return serial_; }	// grows with every change: the UI redraws on it

	private:
		void parseLibrary(const std::string& text);
		void parseProgress(const std::string& text);
		std::string serializeLibrary() const;
		std::string serializeProgress() const;
		void trimSeries();
		void trimProgress();

		std::string dir_;	// the state folder; the files are in <dir>/manga
		std::vector<MangaSeries> series_;
		std::vector<ChapterProgress> progress_;
		mutable std::string savedLibrary_;	// library.tsv as last written (save() skips it when unchanged)
		// chapterId -> its place in progress_, rebuilt after any add or removal (the vector's order changes then)
		mutable std::unordered_map<std::string, size_t> progIdx_;
		mutable bool progIdxDirty_ = true;
		uint64_t serial_ = 0;
	};
}
