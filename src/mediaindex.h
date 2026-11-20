// The cards' media index — the one Music and Videos and Search read (docs/redesign/components.md › "Search and
// Music: the index"). A card holds tens of thousands of files and the handheld has 1 GB, so this is not a listing
// but an answer built once in the background:
//
//   which folders count   a folder named "Music" or "Musics" (any capitals) at the top of each card for songs, one
//                          named "Videos" or "Movies" for films, every such folder on a card read as one, however
//                          deep inside them everything counts, plus BananaOS's own /data/roms/music and
//                          /data/roms/videos when they are there
//   the walk              every audio file under a Music folder is a song and every video there a music video (its
//                          tags read like a song's; without any it goes by its file's name), everything under a
//                          Videos folder a video with no tags read at all. Tags are asked of a caller-given reader
//                          (the worker's libavformat one, a test's fake), only for a file the tag cache
//                          (mediacache.h, keyed by path + size + mtime) doesn't already know
//   the snapshot          the words of the records kept once (an album's songs share their artist and album), the
//                          songs grouped into albums (album artist + album) and the albums into artists, sealed
//                          and shared; MediaIndex hands a copy out with a generation number for a page's
//                          signature()
//   the query             mediaSearch: one pass over the folded names and words, case- and accent-insensitive, every
//                          word of the query somewhere in the item, a hit that starts with it first
//
// Pure library code, no window and no thread: the walk runs on the worker's thread (mediaworker.h), which decides
// when it yields and posts the event when a pass is done.
#pragma once
#include "mediacache.h"
#include "mediatags.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bp
{
	// ---- the folders

	// "Music" or "Musics", any capitals; the Videos folders' names are "Videos" and "Movies"
	bool isMusicFolderName(const std::string& name);
	bool isVideosFolderName(const std::string& name);

	// where the index reads. Each card is a folder at the top of which the Music- and Videos-named folders are
	// looked for (every one of them: a card may have several, read as one list). The cards are BP_INDEX, else
	// BP_BROWSE (the browser's test roots), else the handheld's own two (/data, /run/sd2 when it is mounted); a
	// computer has no cards, so with no BP_* roots there is nothing to read.
	struct MediaFolders
	{
		std::vector<std::string> music, videos;	// full paths; empty: none anywhere
	};
	MediaFolders mediaFolders();
	// the card roots the index reads (BP_INDEX, else BP_BROWSE, else the handheld's own two): for the line under a
	// page's title, which says whether the folders are on one card or both
	std::vector<std::string> mediaCards();
	// "Music on both cards", "Videos and Movies on this card": the folders' kinds as one phrase and which cards hold
	// them ("" when there are none). Music folds "Musics" in; the kinds read in a fixed order, " and " between
	std::string mediaFromWords(const std::vector<std::string>& folders);
	// the card a pass is walking, as the reading notice names it ("" none): the roots' own order, the first "the
	// card" and the removable one "the second card" (the mock-up's wording)
	std::string walkCardName(int card);

	// ---- the Books shelves' folders (the Books page, ui_books.h; the brief's rule: the cards' Books and Documents
	// folders — the index holds songs and videos only, so the shelf lists these itself) ----
	// "Books" or "Documents", any capitals
	bool isBookFolderName(const std::string& name);
	// every such folder at the top of each card, as mediaFolders() walks its own names (never BananaOS's roms: it
	// has no books folder)
	std::vector<std::string> bookFolders();
	// a book file under them, however deep: a path, a size and a date. A folder that can't be read is skipped; a
	// hidden one (a ".hidden" marker) or a dotted name is not walked — the index's rules, for a much smaller list
	struct MediaBook
	{
		std::string path;
		long long size = 0;
		int64_t mtime = 0;
	};
	// by the file's name less its extension, natural and case-insensitive
	std::vector<MediaBook> listBooks(const std::vector<std::string>& folders);

	// ---- the folded word

	// one case- and accent-insensitive form of a text, for matching only: lower-case, the Latin accents off
	// ("Café" -> "cafe"), a mark that only decorates a letter dropped, the other scripts' case lowered as their
	// own (Cyrillic, Greek). Bytes that are not UTF-8 pass through: they match themselves, as anything does
	std::string foldText(const std::string& s);

	// ---- the records

	// the words the records name, kept once: records hold an index into this table instead of their own copies,
	// so fifty thousand songs share their artists' and albums' words rather than carrying them each. Index 0 is
	// the empty word (a tag that isn't there)
	class MediaWords
	{
	public:
		MediaWords() : where{{"", 0}} { strings.emplace_back(); }
		uint32_t add(const std::string& s);
		const std::string& at(uint32_t i) const { return strings[i]; }
		size_t size() const { return strings.size(); }
		// the snapshot is sealed: the dedupe map (half the table's memory) is let go, and no word may be added
		void seal() { where = {}; strings.shrink_to_fit(); }

	private:
		std::vector<std::string> strings;
		std::unordered_map<std::string, uint32_t> where;
	};

	// a song, or a music video, inside a Music folder
	struct MediaSong
	{
		std::string path;										// the file, as the player opens it
		uint32_t title = 0, artist = 0, albumArtist = 0, album = 0;		// the words, shown
		uint32_t foldTitle = 0, foldArtist = 0, foldAlbum = 0;			// the same words, folded, for Search
		int32_t track = 0, disc = 0, year = 0;					// 0: not tagged with one
		int32_t duration = -1;									// seconds; -1 unknown
		bool video = false;										// a music video: VIDEO pill, the video player
																// plays it, Videos doesn't list it
	};

	// grouped by album artist + album: an album's songs in disc, track and name order, the untracked ones
	// (a music video, mostly) after the tracks
	struct MediaAlbum
	{
		uint32_t name = 0, albumArtist = 0;		// the words, shown; 0: an unknown artist
		std::vector<uint32_t> songs;			// indices into the snapshot's songs
	};

	// the album artist when there is one, else the artist; the unknown artist is word 0 and sorts last
	struct MediaArtist
	{
		uint32_t name = 0;
		std::vector<uint32_t> albums;	// indices into the snapshot's albums, in name order
		uint32_t songs = 0;				// under those albums (a music video counts too)
	};

	// a video under a Videos/Movies folder: a name, a folder, a size and a date. Its length the rows read from the
	// durations cache and its place from the resume store (nothing is read for it here), so nothing of it is kept
	// in the record but the file itself
	struct MediaVideo
	{
		std::string path;
		uint32_t foldName = 0;		// the name (less its extension), folded: what Search matches
		long long size = 0;
		int64_t mtime = 0;
	};

	// one whole answer, built off the main thread and only ever read
	struct MediaSnapshot
	{
		MediaWords words;					// sealed: the records share their words through it
		std::vector<MediaSong> songs;		// in the albums' own order
		std::vector<MediaAlbum> albums;		// sorted by artist then name
		std::vector<MediaArtist> artists;	// sorted by name, the unknown artist last
		std::vector<MediaVideo> videos;		// sorted by name
		std::vector<std::string> musicFolders, videoFolders;	// where they came from (the line under Music's title)
		const std::string& at(uint32_t i) const { return words.at(i); }
	};

	// what the UI reads: the newest whole answer, and a generation number that moves with it (a page's
	// signature()). The worker commits on its own thread; a reader only ever holds a finished snapshot
	class MediaIndex
	{
	public:
		void commit(const std::shared_ptr<const MediaSnapshot>& snap);
		std::shared_ptr<const MediaSnapshot> snapshot() const;
		uint32_t generation() const;		// 0 until the first pass is done

	private:
		mutable std::mutex m;
		std::shared_ptr<const MediaSnapshot> snap;
		uint32_t gen = 0;
	};

	// ---- the walk

	// the walk's whole answer
	struct MediaWalk
	{
		MediaWords words;						// the records' words, pooled as they are learned
		std::vector<MediaSong> songs;			// the Music folders' songs and music videos
		std::vector<MediaVideo> videos;			// the Videos folders' videos
		std::vector<std::string> musicFolders, videoFolders;	// those that were there
		std::vector<std::string> failed;		// folders that couldn't be read (logged, not thrown)
		int probed = 0, kept = 0;				// tags read this pass / reused from the cache
	};

	// a file's tags, read on demand: the worker's libavformat reader, a test's fake
	using TagReader = std::function<bool(const std::string& path, long long size, int64_t mtime, MediaTags& out)>;
	// between files: false stops the walk (a quit); it may sleep while the card is someone else's
	using WalkGate = std::function<bool()>;

	// the walk's live counters, for the reading notice's bar (docs/redesign "music-reading",
	// "videos-reading"): written by the walk's thread, read by whoever shows them. The card is the roots' index
	// (mediaCards()'s order) — its name the reader works out from the roots, so no string crosses threads
	struct WalkCounters
	{
		std::atomic<int> card{-1};	// -1: none being read yet
		std::atomic<int> foldersDone{0}, foldersKnown{0};
		std::atomic<int> songs{0}, videos{0};	// read so far this pass
	};
	// one pass over the cards (gate called between every file). A Music file whose path, size and mtime the cache
	// knows is not read again; a changed one has its tags read and the cache updated, so a second start reads only
	// what changed. Nothing is thrown away on the way: a folder that can't be read is one line in failed, the rest
	// go on. false: the walk was stopped (gate), so what it gathered is not a whole answer. `live`, when given, is
	// the pass's counters, kept current as the walk goes
	bool walkMedia(const MediaFolders& folders, const TagReader& tags, MediaTagCache& cache, const WalkGate& gate,
		MediaWalk& out, WalkCounters* live = nullptr);

	// the words a Music folder file is listed by: its tags', an empty title falling back to the file's own name
	// (less its extension) and an empty album to its folder's (a rip puts an album in a folder; a music video with
	// no tags joins its folder's album the same way). song.video is left to the caller: it says what the player
	// opens it as, not what the tags hold.
	void internSong(MediaWords& words, MediaSong& song, const std::string& path, const MediaTags& t);

	// groups the walk's songs into albums (album artist + album) and the albums into artists, each in name order
	// (natural, the unknown artist last), an album's songs in disc, track and name order; folds the videos' names;
	// seals the words. The walk's vectors are consumed.
	std::shared_ptr<const MediaSnapshot> groupMediaSnapshot(MediaWords&& words, std::vector<MediaSong>&& songs,
		std::vector<MediaVideo>&& videos, std::vector<std::string>&& musicFolders, std::vector<std::string>&& videoFolders);

	// ---- the query

	// what Search answers, grouped by kind: indices into the snapshot's songs and videos, and into the caller's
	// own list (Books' and files' names — the parts that are not the index's to hold)
	struct SearchHits
	{
		std::vector<uint32_t> songs, videos, files;
	};

	// one pass over the folded words: every word of the query must sit somewhere in the item (a song's own words:
	// title, artist, album; a video's or file's name, less its extension), and a hit that starts with the query
	// comes before one that only holds it, each group as the lists are sorted. Case and accents never matter.
	// fileNames: shown names, folded here (a page's list lives a moment; the index's own records are pre-folded).
	// The empty query matches nothing.
	SearchHits mediaSearch(const MediaSnapshot& snap, const std::vector<std::string>& fileNames,
		const std::string& query);
}
