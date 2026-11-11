// Saved playlists: extended M3U files in the state folder, and the edits the browser makes on them.
// No window or mpv here; tests/core_test.cpp covers it.
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace bp
{
	struct PlaylistEntry
	{
		std::string path, title;	// path: absolute local, or bpnet://<id>/<path> (never credentials)
		double duration = -1;
	};

	struct Playlist
	{
		std::string file, name;	// file (its full path) is the playlist's id
		std::vector<PlaylistEntry> entries;
	};

	const size_t PL_MAX_FILE_BYTES = 1024 * 1024;
	const size_t PL_MAX_LINE_BYTES = 4096;
	const size_t PL_MAX_ENTRIES = 2000;
	const size_t PL_MAX_LISTED = 500;
	const size_t PL_NAME_MAX_BYTES = 40;

	// Reads out.file's stem as the name when there's no #PLAYLIST line (set out.file first). `skipped` counts what
	// was dropped: photos, non-media, other URL schemes, over-long lines, entries past the cap. False: too big.
	bool parseM3u(const std::string& text, const std::string& m3uDir, Playlist& out, int& skipped);
	std::string serializeM3u(const Playlist&);
	std::string entryTitle(const std::string& path);	// file stem (netBaseName for bpnet)

	enum class PlNameError { None, Empty, TooLong, BadChar, Taken, Builtin };
	// trimmed, 1-40 bytes, no control chars and no '/', unique ignoring ASCII case; renaming may keep its own name
	PlNameError checkPlaylistName(const std::string&, const std::vector<std::string>& existing, const std::string& renaming = "");
	std::string playlistFileName(const std::string& name);	// sanitised stem + ".m3u", no clash handling

	// Editing; each returns remap: old index -> new index, -1 = removed.
	std::vector<int> addEntries(std::vector<PlaylistEntry>&, const std::vector<PlaylistEntry>& add, int at /*-1 end*/, size_t cap, int& dropped);
	std::vector<int> removeEntries(std::vector<PlaylistEntry>&, const std::set<int>& sel);
	std::vector<int> moveSelection(std::vector<PlaylistEntry>&, const std::set<int>& sel, int dir /*-1 up, +1 down*/);
	// The selected ones, in their order, end up starting at index `at` of the final list (clamped to 0..unselected count)
	std::vector<int> moveEntriesTo(std::vector<PlaylistEntry>&, const std::set<int>& sel, int at);
	std::vector<int> gatherSelection(std::vector<PlaylistEntry>&, const std::set<int>& sel);	// block at the first selected's place, order kept
	std::vector<int> removeDuplicates(std::vector<PlaylistEntry>&, int& removed);	// keeps each path's first
	std::set<int> remapSelection(const std::set<int>&, const std::vector<int>& remap);
	// A removed one gives the next survivor's new index; with none after it, the last survivor's; -1 if none survive.
	int remapIndex(int i, const std::vector<int>& remap);
	std::vector<int> shuffleOrder(int n, int first, uint64_t seed);	// a permutation with `first` first (any, if out of range)
	// dot names skipped; isVideoFile||isAudioFile kept; photos counted; others ignored; naturalLess; netJoin for bpnet
	std::vector<PlaylistEntry> folderEntries(const std::string& dir, const std::vector<std::string>& names, int& skippedPhotos);

	bool writeFileAtomic(const std::string& path, const std::string& text);	// tmp + rename; false on failure
	bool writeFileDurable(const std::string& path, const std::string& text);	// the same, synced to the card (fsync)

	class PlaylistStore
	{
	public:
		explicit PlaylistStore(std::string dir);	// created by mkdir on first save
		// The built-in Liked playlist: stored as ".liked.m3u". A name that starts with '.' can't be a user's playlist
		// (playlistFileName trims leading dots, and list() skips dot files), so it never collides with one the user
		// named "Liked"; both can exist and only this one is `builtin`. Its id (the file's stem) is LIKED_ID.
		static constexpr const char* LIKED_ID = ".liked";
		static constexpr const char* LIKED_NAME = "Liked";
		struct Info { std::string file, name; int count; bool builtin = false; };
		std::vector<Info> list() const;	// Liked first (always, even with no file yet), then naturalLess by name
		std::vector<Info> addTargets() const;	// list() without Liked: where Add to Playlist can put things
		bool isBuiltinFile(const std::string& file) const;	// Liked's file
		std::string likedFile() const { return dir_ + "/.liked.m3u"; }
		// Liked, managed only by the Like button. Only songs and videos (never photos) can be liked; paths are local or
		// bpnet://. isLiked is a cached set (rebuilt when serial() moved), cheap enough for every frame.
		static bool likable(const std::string& path);
		bool isLiked(const std::string& path) const;
		// Idempotent: liking a liked one is a no-op, and unliking removes every copy. A new one goes first (newest
		// first); past PL_MAX_ENTRIES the oldest fall off. False: not likable, or the file couldn't be written.
		// at: where a new one goes (0 the top; an Undo puts one back in its place, clamped to the end). dropped: the
		// path that fell off the end ("" none)
		bool setLiked(const std::string& path, const std::string& title, double duration, bool on, int at = 0,
			std::string* dropped = nullptr);
		int likedCount() const;
		// Is `path` one of the playlist `file`'s entries (for "already in this playlist")
		bool containsPath(const std::string& file, const std::string& path) const;
		bool load(const std::string& file, Playlist&, int& skipped) const;
		bool save(const Playlist&);
		PlNameError create(const std::string& name, std::string& fileOut);
		PlNameError rename(const std::string& file, const std::string& newName, std::string& newFileOut);	// new written, old unlinked
		bool remove(const std::string& file);	// false for Liked
		int serial() const { return serial_; }	// bumped by create/rename/remove/save
		const std::string& dir() const { return dir_; }

	private:
		std::string uniquePath(const std::string& name, const std::string& allowSame) const;
		void likedRefresh() const;
		std::string dir_;
		int serial_ = 0;
		mutable int likedSerial_ = -1;
		mutable std::set<std::string> likedSet_;
	};
}
