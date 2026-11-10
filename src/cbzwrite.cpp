#include "cbzwrite.h"
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <zlib.h>

namespace bp
{
	namespace
	{
		// The reader's own caps (cbz.cpp): an archive past them isn't a chapter, and without ZIP64 our fields
		// couldn't honestly hold the bigger numbers anyway
		const uint64_t kMaxEntries = 10000;
		const uint64_t kMaxTotal = 4ull << 30;
		const uint64_t kMaxFile = 64ull << 20;

		// 1980-01-01 00:00:00, the DOS fields' zero point: fixed, so the same chapter written twice is the
		// same bytes and a re-download can be checked against the file on the card
		const uint16_t kDosTime = 0;
		const uint16_t kDosDate = 33;	// (year 1980 - 1980) << 9 | month 1 << 5 | day 1

		void putBytes(std::vector<unsigned char>& v, const char* p, size_t n)
		{
			v.insert(v.end(), (const unsigned char*)p, (const unsigned char*)p + n);
		}

		// ZIP is little endian, whatever the machine is
		void put16(std::vector<unsigned char>& v, uint16_t x)
		{
			v.push_back((unsigned char)(x & 0xff));
			v.push_back((unsigned char)(x >> 8));
		}

		void put32(std::vector<unsigned char>& v, uint32_t x)
		{
			for (int k = 0; k < 4; k++) v.push_back((unsigned char)((x >> (8 * k)) & 0xff));
		}
	}

	CbzWriter::~CbzWriter()
	{
		if (f) abort();
	}

	bool CbzWriter::bad(const std::string& msg)
	{
		err = msg;
		return false;
	}

	bool CbzWriter::put(const void* p, size_t n)
	{
		if (n && std::fwrite(p, 1, n, f) != n) return false;
		pos += n;
		return true;
	}

	// An entry's name is built from titles a server sent us, so it has to stay a plain relative path: pages
	// read by other tools should never be able to land outside the chapter's folder
	bool CbzWriter::nameOk(const std::string& name)
	{
		if (name.empty()) return bad("an entry needs a name");
		if (name.size() > 0xFFFF) return bad("an entry name is at most 64 KB: the length fields are 16 bits");
		if (name[0] == '/') return bad("\"" + name + "\": absolute names are refused");
		if (name.find("..") != std::string::npos) return bad("\"" + name + "\": a name never holds \"..\"");
		if (name.find('\\') != std::string::npos) return bad("\"" + name + "\": folders are separated by \"/\"");
		return true;
	}

	bool CbzWriter::open(const std::string& path)
	{
		if (f) return bad("an archive is already open here: finish() or abort() it first");
		err.clear();
		finalPath = path;
		partPath = path + ".part";
		// "wb": a stale .part from an interrupted earlier run is replaced, not appended to - offsets from a
		// writer that's gone would only make a broken archive
		f = std::fopen(partPath.c_str(), "wb");
		if (!f)
		{
			finalPath.clear();
			partPath.clear();
			return bad(path + ": " + std::strerror(errno));
		}
		pos = 0;
		recs.clear();
		names.clear();
		return true;
	}

	bool CbzWriter::add(const std::string& name, const uint8_t* data, size_t size)
	{
		err.clear();
		if (!f) return bad("no archive is open");
		if (!nameOk(name)) return false;
		if (recs.size() >= kMaxEntries) return bad("an archive holds at most 10000 entries");
		if (pos + size >= kMaxTotal) return bad("an archive holds at most 4 GB: no ZIP64");
		if (!names.insert(name).second) return bad("\"" + name + "\": already in the archive");

		uint32_t crc = (uint32_t)crc32(0, data, (uInt)size);	// size is under the caps, so a uInt holds it
		// Info-ZIP's language flag, bit 11: set unless the name is plain ASCII, so a Japanese title reads
		// right in other tools
		uint16_t flags = 0;
		for (char c : name)
			if ((unsigned char)c >= 0x80) { flags = 0x800; break; }

		std::vector<unsigned char> lh;
		lh.reserve(30 + name.size());
		putBytes(lh, "PK\x03\x04", 4);
		put16(lh, 20);				// version needed: 2.0, stored entries and no ZIP64
		put16(lh, flags);
		put16(lh, 0);				// method 0, stored: pages are compressed already, deflating them again
									// would cost the RK3568 and save nothing
		put16(lh, kDosTime);
		put16(lh, kDosDate);
		put32(lh, crc);
		put32(lh, (uint32_t)size);	// packed == size: stored
		put32(lh, (uint32_t)size);
		put16(lh, (uint16_t)name.size());
		put16(lh, 0);				// no extra field
		putBytes(lh, name.data(), name.size());

		const uint32_t offset = (uint32_t)pos;
		if (!put(lh.data(), lh.size()) || (size && !put(data, size)))
		{
			discard();	// a half-written entry leaves bytes at a wrong offset: the part can't be trusted
			return bad("could not write the archive: " + std::string(std::strerror(errno)));
		}
		recs.push_back(Rec{name, crc, (uint32_t)size, offset, flags});
		return true;
	}

	bool CbzWriter::addFile(const std::string& name, const std::string& path)
	{
		err.clear();
		if (!f) return bad("no archive is open");
		FILE* in = std::fopen(path.c_str(), "rb");
		if (!in) return bad(path + ": " + std::strerror(errno));
		bool ok = fseeko(in, 0, SEEK_END) == 0;
		off_t size = ok ? ftello(in) : (off_t)-1;
		if (!ok || size < 0)
		{
			std::fclose(in);
			return bad(path + ": " + std::strerror(errno));
		}
		if ((uint64_t)size > kMaxFile)
		{
			std::fclose(in);
			return bad(path + ": larger than 64 MB");
		}
		// The whole file is held while it is written: a page is far under the cap, and one read keeps the
		// CRC and the copy in one pass
		std::vector<uint8_t> buf((size_t)size);
		ok = fseeko(in, 0, SEEK_SET) == 0
			&& (size == 0 || std::fread(buf.data(), 1, (size_t)size, in) == (size_t)size);
		std::fclose(in);
		if (!ok) return bad(path + ": " + std::strerror(errno));
		return add(name, buf.data(), buf.size());
	}

	bool CbzWriter::finish()
	{
		err.clear();
		if (!f) return bad("no archive is open");

		// One record per entry, the fields the central directory keeps for it (cbz.h reads exactly these)
		std::vector<unsigned char> cd;
		for (const Rec& r : recs)
		{
			size_t room = cd.size() + 46 + r.name.size();
			cd.reserve(room);
			putBytes(cd, "PK\x01\x02", 4);
			put16(cd, 20);			// made by and needed: 2.0, as the local headers
			put16(cd, 20);
			put16(cd, r.flags);
			put16(cd, 0);			// stored
			put16(cd, kDosTime);
			put16(cd, kDosDate);
			put32(cd, r.crc);
			put32(cd, r.size);
			put32(cd, r.size);
			put16(cd, (uint16_t)r.name.size());
			put16(cd, 0);			// no extra, no comment
			put16(cd, 0);
			put16(cd, 0);			// one disk, no attributes: nothing a CBZ reader asks for
			put16(cd, 0);
			put32(cd, 0);
			put32(cd, r.offset);
			putBytes(cd, r.name.data(), r.name.size());
		}
		const uint64_t cdAt = pos, cdSize = cd.size();
		if (!put(cd.data(), cd.size()))
		{
			discard();
			return bad("could not write the archive: " + std::string(std::strerror(errno)));
		}

		std::vector<unsigned char> eocd;
		eocd.reserve(22);
		putBytes(eocd, "PK\x05\x06", 4);
		put16(eocd, 0);				// this disk...
		put16(eocd, 0);				// ...and the one the directory starts on
		put16(eocd, (uint16_t)recs.size());
		put16(eocd, (uint16_t)recs.size());
		put32(eocd, (uint32_t)cdSize);
		put32(eocd, (uint32_t)cdAt);	// under the caps these all still fit 32 bits: past them add() refused
		put16(eocd, 0);				// no comment: cbz.h believes an end record whose comment length brings
										// it to the file's very end, and this one does
		if (!put(eocd.data(), eocd.size()))
		{
			discard();
			return bad("could not write the archive: " + std::string(std::strerror(errno)));
		}

		// The bytes are on the card before the name is: rename() is atomic, so a reader finds either the old
		// state or a complete chapter, never a half one (docs/design/manga.md 7)
		if (std::fflush(f) != 0 || fsync(::fileno(f)) != 0)
		{
			discard();
			return bad("could not flush the archive: " + std::string(std::strerror(errno)));
		}
		std::fclose(f);
		f = nullptr;
		if (std::rename(partPath.c_str(), finalPath.c_str()) != 0)
		{
			const std::string msg = finalPath + ": could not be put in place: " + std::strerror(errno);
			discard();	// the part never made it: drop it rather than leave one no one will finish
			return bad(msg);
		}
		clearState();
		return true;
	}

	void CbzWriter::abort()
	{
		discard();
	}

	void CbzWriter::clearState()
	{
		recs.clear();
		names.clear();
		pos = 0;
		finalPath.clear();
		partPath.clear();
	}

	void CbzWriter::discard()
	{
		if (f)
		{
			std::fclose(f);
			f = nullptr;
		}
		if (!partPath.empty()) std::remove(partPath.c_str());
		clearState();
	}
}
