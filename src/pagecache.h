#pragma once
// The manga reader's page cache: the pages kept on the card, so a chapter can be read again without fetching it
// (docs/design/manga.md, section 6). A folder of files named by a hash of the key, and an index with the sizes and
// the last use, so the least recently used pages can go when the folder grows over its cap. Plain files, no window,
// no threads and no network: the reader's worker hands a page over and asks for it back.
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace bp
{
	class PageCache
	{
	public:
		~PageCache();

		// The cache folder (the state dir's <state>/manga/cache) and the cap in bytes: over the cap the least
		// recently used pages go first. The folder is made when it isn't there. A missing or damaged index is no
		// trouble: the sizes are rebuilt from the files and the last use from their mtime. False when the folder
		// can't be used (no folder, no name).
		bool open(const std::string& dir, uint64_t limitBytes);
		// A new cap, applied at once: over it, the least recently used pages go.
		void setLimit(uint64_t limitBytes);
		// Where the page with this key lives. The same key always gives the same path, and the path is always
		// inside the folder: the name is the key's hash, so it holds no path of its own.
		std::string pathFor(const std::string& key) const;
		// Whether the page is there. It counts as used, so it isn't the next to go.
		bool has(const std::string& key);
		// A page is written beside the others and renamed over, so a half page never looks whole. False when the
		// page is bigger than the cap (it would never fit) or when it can't be written.
		bool put(const std::string& key, const uint8_t* data, size_t n);
		// The page, when it is there. It counts as used.
		bool get(const std::string& key, std::vector<uint8_t>& out);
		// Every page goes, and the index with it: the folder is as it was before the reader was ever on.
		void clear();
		// The bytes in the folder and how many pages there are.
		uint64_t size() const;
		size_t count() const;
		// The index, written beside and renamed over. The destructor does it too when something changed, so the
		// order of the pages survives a restart without the reader having to remember to save it.
		bool save() const;

	private:
		// A page in the folder. The key is what the index knows it by; a page whose index was lost has only its
		// file name, and the key is filled in again when the page is asked for.
		struct Entry
		{
			std::string key;
			uint64_t bytes = 0;
			uint64_t lastUse = 0;
		};

		void loadIndex();
		void scanFiles();
		void addFile(const std::string& path, uint64_t bytes, uint64_t lastUse);
		void evictToLimit();
		void removeContents(const std::string& dir);
		uint64_t stamp();

		std::string dir_;		// the cache folder
		uint64_t limit_ = 0;	// the cap in bytes
		uint64_t total_ = 0;	// the bytes in the folder
		uint64_t stamp_ = 0;	// the last stamp handed out, so every use is later than the one before
		std::unordered_map<std::string, Entry> entries_;	// by path: the path is the key's hash, so it is known
		mutable bool dirty_ = false;	// the index is out of date
	};
}
