#pragma once
// Writing a CBZ: the counterpart of cbz.h for downloads (docs/design/manga.md 7). Pages are appended one at a
// time into <final path>.part, and finish() writes the directory and renames the file in one go, so a half
// chapter never looks whole. Everything is stored (method 0): pages are JPEG/PNG already, deflating them again
// would cost the RK3568 for nothing. No ZIP64, so the writer refuses past 10000 entries and 4 GB - the same
// caps cbz.h reads under - and a chapter is a few hundred pages well inside them. One archive at a time, on one
// thread: the download worker owns an instance while it runs.
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace bp
{
	class CbzWriter
	{
	public:
		CbzWriter() = default;
		~CbzWriter();	// an archive dropped before finish() is aborted: no stray .part is left behind
		CbzWriter(const CbzWriter&) = delete;
		CbzWriter& operator=(const CbzWriter&) = delete;

		// start writing to finalPath + ".part"; a stale .part (an interrupted earlier download) is replaced.
		// false with error() set when the part can't be created, or an archive is already open here
		bool open(const std::string& finalPath);

		// append one stored entry. Refuses, with error() set, a name that is empty, absolute, holds ".." or
		// "\", or was added before, and an archive past 10000 entries or 4 GB. Names are UTF-8; the archive's
		// UTF-8 flag (bit 11) is set when a name isn't ASCII
		bool add(const std::string& name, const uint8_t* data, size_t size);

		// add() with a file's contents, read into memory first. Refuses files over 64 MB: cbz.h won't read a
		// bigger entry back, so such a page would sit in the chapter unread
		bool addFile(const std::string& name, const std::string& path);

		// write the central directory and end record, fsync, close, and rename the .part onto finalPath. Any
		// failure removes the .part: what's left is either nothing or a complete chapter
		bool finish();

		void abort();	// close and delete the .part; harmless once finish() has moved it

		const std::string& error() const { return err; }

	private:
		// what add() remembers of each entry until finish() puts it in the central directory
		struct Rec
		{
			std::string name;
			uint32_t crc = 0, size = 0, offset = 0;
			uint16_t flags = 0;
		};

		bool bad(const std::string& msg);
		bool put(const void* p, size_t n);
		bool nameOk(const std::string& name);
		void clearState();
		void discard();	// close, delete the .part and forget the in-memory half too

		FILE* f = nullptr;
		uint64_t pos = 0;	// bytes written so far: an entry's offset and the directory's size and place
		std::string finalPath, partPath, err;
		std::vector<Rec> recs;
		std::set<std::string> names;
	};
}
