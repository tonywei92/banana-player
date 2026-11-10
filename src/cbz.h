#pragma once
// Reading a CBZ: a ZIP of page images, how a chapter is stored on the card and how downloads are saved
// (docs/design/manga.md 5.1 and 7). open() reads the central directory once and read() inflates one entry at a
// time, so a two-hundred-page chapter costs only the page asked for; the file handle stays open between calls.
// Stored and deflated entries; ZIP64 archives are refused, and an encrypted entry is listed but never reads.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bp
{
	// one file of the archive, as its central directory record describes it
	struct CbzEntry
	{
		std::string name;
		uint32_t method = 0;	// 0 stored, 8 deflate; anything else we cannot read
		uint64_t packed = 0, size = 0;	// the compressed and the uncompressed size
		uint32_t crc = 0;
		uint64_t localOffset = 0;	// where the entry's local header sits in the file
	};

	class Cbz
	{
	public:
		Cbz() = default;
		~Cbz();
		Cbz(const Cbz&) = delete;
		Cbz& operator=(const Cbz&) = delete;

		// reads the end-of-central-directory (searching the last 64 KB + 22, so a ZIP comment is fine) and the
		// central directory; false with error() set when the file isn't there, isn't a ZIP, or is one we refuse:
		// ZIP64, multi-disk, more than 10000 entries, a directory over 16 MB
		bool open(const std::string& path);
		void close();
		bool isOpen() const { return f != nullptr; }
		const std::string& error() const { return err; }

		const std::vector<CbzEntry>& entries() const { return ent; }

		// the page images among the entries (isPageImage) in reading order (sortPages)
		std::vector<std::string> pages() const;

		// the uncompressed bytes of one entry; false with error() set when the name isn't in the archive, the
		// entry is encrypted, its method isn't stored or deflate, it is over 64 MB uncompressed, its data lies
		// outside the file, or it fails to check out (CRC, size, a broken deflate stream)
		bool read(const std::string& name, std::vector<uint8_t>& out);

	private:
		bool at(uint64_t off, void* dst, size_t n);
		bool bad(const std::string& msg);	// set error(), return false
		bool fail(const std::string& msg);	// bad(), and drop the half-read archive too: only open() uses it

		FILE* f = nullptr;
		uint64_t fileSize = 0;
		std::vector<CbzEntry> ent;
		std::vector<uint16_t> entFlags;	// the general purpose bits, parallel to ent (CbzEntry has no room)
		std::string err;
	};
}
