#pragma once
// The manga reader's library and reading progress: what the reader must remember across restarts
// (docs/design/manga.md, section 6). Two TSV files in <state>/manga: library.tsv and progress.tsv. Plain data, no
// window, no threads and no clock of its own: the reader's pages read it and the shell saves it.
#include "writequeue.h"	// FileWrite: the texts save() writes, for a caller that writes them off the main thread
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
		float within = 0;	// Vertical scroll: how far into the page (0..1), so a tall page (a PDF's) resumes where it was
		int64_t at = 0;	// when it was last here
		std::vector<int> marks;	// the pages bookmarked (0-based, ascending): the reader's More and the Pages grid's tab
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
		// The same two files as save(), serialized on the calling thread for a writer thread to put on the card:
		// the reader's loop copies the texts into its queue instead of paying the fsync.
		std::vector<FileWrite> filesToSave() const;

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
		// every series' id saved (the card's are "local:<path>")
		std::vector<std::string> seriesIds() const;

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
		// A page's bookmark, put or taken away (true: it is bookmarked now). They are kept with the chapter's place,
		// which a chapter never opened gets for them (hidden from the History, as markRead's), and setProgress never
		// touches them: at most MAX_MARKS a chapter.
		static constexpr size_t MAX_MARKS = 200;
		bool toggleBookmark(const std::string& seriesId, const std::string& chapterId, int page);
		std::vector<int> bookmarks(const std::string& chapterId) const;	// ascending; none: empty
		// The reading history: every chapter is hidden from the History, the reading progress stays (the read
		// marks and the page to resume).
		void clearHistory();
		// the History rows read at or after this time (seconds since the epoch) leave the list; the places stay
		void clearHistorySince(int64_t since);
		// one series' rows leave the list (History's Selected item); its places stay
		void clearHistorySeries(const std::string& seriesId);
		// How many of the chapters given aren't read.
		int unread(const std::string& seriesId, const std::vector<std::string>& chapterIds) const;

		// A downloaded chapter's file linked to the source chapter it is a copy of (issue #15): one place for both.
		// Every call above that takes a chapter id takes the file's path as that chapter's id from then on, and
		// keeps the place under the source's ids (chapterId, seriesId). A place already kept under the path moves
		// there; one kept under both is merged (the later place, read when either is). Not saved: the links come
		// from the files each run (ComicInfo's BPChapter, or a match in the source's chapter list), at most
		// MAX_PROGRESS of them.
		enum class Link { Same, Linked, Moved, Merged };
		Link linkChapter(const std::string& path, const std::string& chapterId, const std::string& seriesId);
		// the id a chapter's place is kept under: the source's for a linked file, else the id as given
		const std::string& canonicalChapter(const std::string& chapterId) const;

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
		// chapterId -> its place in progress_, rebuilt after any add or removal (the vector's order changes then)
		mutable std::unordered_map<std::string, size_t> progIdx_;
		mutable bool progIdxDirty_ = true;
		struct LinkTo { std::string chapterId, seriesId; };
		std::unordered_map<std::string, LinkTo> links_;	// a downloaded file's path -> its source chapter
		const LinkTo* linkOf(const std::string& chapterId) const;
		uint64_t serial_ = 0;
	};
}
